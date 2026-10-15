#include "HRDatabase.h"
#include "HRDatabaseConnection.h"
#include "HRDatabaseSchema.h"
#include "HRDatabasePolicy.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "QueryResult.h"
#include <errmsg.h>
#include <mysqld_error.h>
#include <limits>

uint32 HRConnection::Open()
{
    // The core changes the Unix socket marker to localhost. Restore our own
    // transport before every open, without changing the live core pool.
    _info.host = _host;
    _connectionLost = false;
    return MySQLConnection::Open();
}

bool HRConnection::EnsureOpen()
{
    return m_Mysql || Open() == 0;
}

bool HRConnection::_HandleMySQLErrno(uint32 error, char const*, uint8)
{
    _connectionLost = error == CR_SERVER_GONE_ERROR || error == CR_SERVER_LOST ||
        error == CR_SERVER_LOST_EXTENDED || error == CR_CONN_HOST_ERROR;
    // Replaying one statement after reconnecting can split a transaction into
    // autocommitted fragments. Report the failure; the next operation may reopen.
    // Module SQL errors also must not invoke the core handler's process abort.
    return false;
}

void HRConnection::FinishOperation()
{
    if (_connectionLost)
        Close();
}

HRDatabase& HRDatabase::Instance()
{
    static HRDatabase database;
    return database;
}

bool HRDatabase::Open(std::string const& connectionString, std::string const& databaseName)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_connection)
        return true;
    if (connectionString.empty())
    {
        // Reuse the core's authentication, but own a separate connection and schema.
        _info = HRCopyPrivateDatabaseConnection(CharacterDatabase.GetConnectionInfo(), databaseName);
        if (!_info)
        {
            LOG_ERROR("server.loading", "HabilidadesRandom: la conexión de personajes del core aún no está disponible.");
            return false;
        }
    }
    else
        _info = std::make_unique<MySQLConnectionInfo>(connectionString);
    std::vector<std::string> coreDatabases;
    for (auto pool : {WorldDatabase.GetConnectionInfo(), CharacterDatabase.GetConnectionInfo(), LoginDatabase.GetConnectionInfo()})
        if (pool)
            coreDatabases.push_back(pool->database);
    bool valid = HRValidPrivateDatabaseName(_info->database, coreDatabases);
    if (!valid || !HRValidPrivateDatabaseTransport(*_info))
    {
        LOG_ERROR("server.loading", "HabilidadesRandom: conexión o nombre de base propia inválidos; debe ser distinta de las bases del core.");
        return false;
    }
    _connection = std::make_unique<HRConnection>(*_info);
    uint32 error = _connection->Open();
    if (error == ER_BAD_DB_ERROR)
    {
        // Existing private databases do not require CREATE DATABASE at every
        // restart. Bootstrap only when this selected database does not exist.
        auto bootstrapInfo = HRCopyPrivateDatabaseConnection(_info.get(), "information_schema");
        HRConnection bootstrap(*bootstrapInfo);
        if (bootstrap.Open() || !bootstrap.Execute("CREATE DATABASE IF NOT EXISTS `" + _info->database + "` CHARACTER SET utf8mb4"))
        {
            LOG_ERROR("server.loading", "HabilidadesRandom: no se pudo crear la base propia {}; comprueba el permiso CREATE de la cuenta del core.", _info->database);
            _connection.reset();
            return false;
        }
        error = _connection->Open();
    }
    if (error || !InitializeSchema())
    {
        _connection.reset();
        return false;
    }
    return true;
}

bool HRDatabase::InitializeSchema()
{
    // Reject unknown versions before changing their schema. A successful COUNT
    // distinguishes a new installation from an inaccessible or damaged backup.
    auto meta = Query("SELECT COUNT(*) FROM information_schema.tables WHERE TABLE_SCHEMA=DATABASE() "
        "AND TABLE_NAME='mod_habilidades_random_meta'");
    if (!meta)
        return false;
    uint32 currentVersion = 0;
    if (meta->Fetch()[0].Get<uint32>())
    {
        auto count = Query("SELECT COUNT(*) FROM mod_habilidades_random_meta WHERE id=1");
        if (!count)
            return false;
        if (count->Fetch()[0].Get<uint32>())
        {
            auto version = Query("SELECT version FROM mod_habilidades_random_meta WHERE id=1");
            if (!version)
                return false;
            currentVersion = version->Fetch()[0].Get<uint32>();
            if (currentVersion != 1 && currentVersion != HRSchema::Version)
            {
                LOG_ERROR("server.loading", "HabilidadesRandom: versión de esquema no compatible {}.", currentVersion);
                return false;
            }
        }
    }
    for (auto statement : HRSchema::Statements)
        if (!_connection->Execute(statement))
            return false;
    // Earlier releases omitted an engine for these two tables. In particular,
    // the seed/version marker must participate in the same rollback as the pool.
    for (std::string const table : {"mod_habilidades_random_meta", "mod_habilidades_random_npcs"})
    {
        auto engine = Query("SELECT ENGINE FROM information_schema.tables WHERE TABLE_SCHEMA=DATABASE() "
            "AND TABLE_NAME='" + table + "'");
        if (!engine || (engine->Fetch()[0].Get<std::string>() != "InnoDB" &&
            !_connection->Execute("ALTER TABLE `" + table + "` ENGINE=InnoDB")))
            return false;
    }
    if (currentVersion == HRSchema::Version)
        return true;
    // Version 2 adds only skill snapshot tables. Unknown historical skill values
    // remain unknown; an existing character backup is never synthesized.
    if (currentVersion == 1)
        return _connection->Execute("UPDATE mod_habilidades_random_meta SET version=" +
            std::to_string(HRSchema::Version) + " WHERE id=1 AND version=1");
    auto transaction = std::make_shared<HRTransaction>();
    for (auto const& entry : HRSchema::Pool)
        transaction->Append("INSERT IGNORE INTO mod_habilidades_random_spell_rarity VALUES (" +
            std::to_string(entry.id) + ',' + std::to_string(entry.rarity) + ')');
    transaction->Append("INSERT INTO mod_habilidades_random_meta VALUES (1," + std::to_string(HRSchema::Version) + ')');
    return Commit(transaction);
}

QueryResult HRDatabase::Query(std::string const& sql)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_connection || !_connection->EnsureOpen())
        return QueryResult(nullptr);
    std::unique_ptr<ResultSet> result(_connection->Query(sql));
    _connection->FinishOperation();
    if (!result || !result->GetRowCount() || !result->NextRow())
        return QueryResult(nullptr);
    return QueryResult(result.release());
}

bool HRDatabase::Commit(std::shared_ptr<HRTransaction> const& transaction)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_connection || !transaction || !_connection->EnsureOpen())
        return false;
    bool committed = HRExecuteCheckedTransaction(*transaction, [this](std::string const& sql)
    {
        return _connection->Execute(sql);
    });
    // A failed COMMIT may have reached the server. Never claim success or reuse
    // an uncertain transaction; closing also rolls back any still-open work.
    if (!committed)
        _connection->Close();
    return committed;
}

bool HRDatabase::Execute(std::string const& sql)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_connection || !_connection->EnsureOpen())
        return false;
    bool success = _connection->Execute(sql);
    _connection->FinishOperation();
    return success;
}

bool HRDatabase::ExecuteInsert(std::string const& sql, uint32& insertedId)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    insertedId = 0;
    if (!_connection || !_connection->EnsureOpen())
        return false;
    if (!_connection->Execute("START TRANSACTION"))
    {
        _connection->Close();
        return false;
    }
    auto fail = [this]()
    {
        _connection->Execute("ROLLBACK");
        _connection->Close();
        return false;
    };
    if (!_connection->Execute(sql))
        return fail();
    // Keep the insert and its connection-local ID under one lock and one
    // transaction. A disconnect must never reopen and return an unrelated ID.
    std::unique_ptr<ResultSet> result(_connection->Query("SELECT LAST_INSERT_ID()"));
    if (!result || !result->GetRowCount() || !result->NextRow())
        return fail();
    uint64 id = result->Fetch()[0].Get<uint64>();
    result.reset();
    if (!id || id > std::numeric_limits<uint32>::max() || !_connection->Execute("COMMIT"))
        return fail();
    insertedId = static_cast<uint32>(id);
    return true;
}

void HRDatabase::KeepAlive()
{
    // Ping does not use the core's error callback. A buffered trivial query
    // detects a stale connection and lets a later operation reopen it safely.
    Query("SELECT 1");
}

void HRDatabase::Close()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _connection.reset();
    _info.reset();
}

bool HRDatabase::IsOpen() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return bool(_connection);
}
