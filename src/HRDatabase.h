#ifndef HR_DATABASE_H
#define HR_DATABASE_H
#include "DatabaseEnvFwd.h"
#include "MySQLConnection.h"
#include "HRDatabaseConnection.h"
#include <memory>
#include <mutex>
#include <string>

class HRConnection : public MySQLConnection
{
public:
    explicit HRConnection(MySQLConnectionInfo& info) : MySQLConnection(info), _info(info), _host(info.host) { }
    uint32 Open() override;
    bool EnsureOpen();
    void FinishOperation();
protected:
    void DoPrepareStatements() override { }
    bool _HandleMySQLErrno(uint32 error, char const* message = "", uint8 attempts = 5) override;
private:
    bool _connectionLost = false;
    MySQLConnectionInfo& _info;
    std::string const _host;
};

class HRDatabase
{
public:
    static HRDatabase& Instance();
    bool Open(std::string const& connectionString, std::string const& databaseName = "mod_habilidades_random");
    void Close();
    void KeepAlive();
    QueryResult Query(std::string const& sql);
    bool Commit(std::shared_ptr<HRTransaction> const& transaction);
    bool Execute(std::string const& sql);
    bool ExecuteInsert(std::string const& sql, uint32& insertedId);
    bool IsOpen() const;
private:
    bool InitializeSchema();
    mutable std::recursive_mutex _mutex;
    std::unique_ptr<MySQLConnectionInfo> _info;
    std::unique_ptr<HRConnection> _connection;
};
#define HRDatabaseStore HRDatabase::Instance()
#endif
