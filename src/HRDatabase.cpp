#include "HRDatabase.h"
#include "HRDatabaseConnection.h"
#include "HRDatabaseSchema.h"
#include "HRDatabasePolicy.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "QueryResult.h"
#include "Transaction.h"

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
    if (!valid || _info->host.empty() || _info->port_or_socket.empty() || _info->user.empty())
    {
        LOG_ERROR("server.loading", "HabilidadesRandom: conexión o nombre de base propia inválidos; debe ser distinta de las bases del core.");
        return false;
    }
    // Create only the explicitly configured private schema. Never use a core database for bootstrapping.
    auto bootstrapInfo = *_info;
    bootstrapInfo.database = "information_schema";
    HRConnection bootstrap(bootstrapInfo);
    if (bootstrap.Open())
        return false;
    if (!bootstrap.Execute("CREATE DATABASE IF NOT EXISTS `" + _info->database + "` CHARACTER SET utf8mb4"))
    {
        LOG_ERROR("server.loading", "HabilidadesRandom: la cuenta MySQL utilizada necesita permiso CREATE sobre la base propia {}.", _info->database);
        return false;
    }
    _connection = std::make_unique<HRConnection>(*_info);
    if (_connection->Open() || !InitializeSchema())
    {
        _connection.reset();
        return false;
    }
    return true;
}

bool HRDatabase::InitializeSchema()
{
    for (auto statement : HRSchema::Statements)
        if (!_connection->Execute(statement))
            return false;
    // SELECT with no rows is not proof of a missing version. COUNT must succeed first.
    auto count = Query("SELECT COUNT(*) FROM mod_habilidades_random_meta WHERE id=1");
    if (!count)
        return false;
    if (count->Fetch()[0].Get<uint32>())
    {
        auto version = Query("SELECT version FROM mod_habilidades_random_meta WHERE id=1");
        if (!version)
            return false;
        uint32 currentVersion = version->Fetch()[0].Get<uint32>();
        if (currentVersion == HRSchema::Version)
            return true;
        // Version 2 adds only the skill snapshot tables created above. Existing
        // event backups remain valid; their missing historical skills are unknown.
        if (currentVersion == 1)
            return _connection->Execute("UPDATE mod_habilidades_random_meta SET version=" +
                std::to_string(HRSchema::Version) + " WHERE id=1 AND version=1");
        LOG_ERROR("server.loading", "HabilidadesRandom: versión de esquema no compatible {}.", currentVersion);
        return false;
    }
    auto transaction = std::make_shared<TransactionBase>();
    for (auto const& entry : HRSchema::Pool)
        transaction->Append("INSERT IGNORE INTO mod_habilidades_random_spell_rarity VALUES (" +
            std::to_string(entry.id) + ',' + std::to_string(entry.rarity) + ')');
    transaction->Append("INSERT INTO mod_habilidades_random_meta VALUES (1," + std::to_string(HRSchema::Version) + ')');
    return Commit(transaction);
}

QueryResult HRDatabase::Query(std::string const& sql)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_connection)
        return QueryResult(nullptr);
    std::unique_ptr<ResultSet> result(_connection->Query(sql));
    if (!result || !result->GetRowCount() || !result->NextRow())
        return QueryResult(nullptr);
    return QueryResult(result.release());
}

bool HRDatabase::Commit(std::shared_ptr<TransactionBase> const& transaction)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _connection && _connection->ExecuteTransaction(transaction) == 0;
}

bool HRDatabase::Execute(std::string const& sql)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _connection && _connection->Execute(sql);
}

void HRDatabase::KeepAlive()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_connection)
        _connection->Ping();
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
