#ifndef HR_DATABASE_CONNECTION_H
#define HR_DATABASE_CONNECTION_H

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Private transactions contain only module SQL, so every boundary can be
// checked without inheriting the core's unchecked START/COMMIT behavior.
class HRTransaction
{
public:
    void Append(std::string_view sql) { _statements.emplace_back(sql); }
    std::vector<std::string> const& Statements() const { return _statements; }
private:
    std::vector<std::string> _statements;
};

template<class Transaction, class Execute>
bool HRExecuteCheckedTransaction(Transaction const& transaction, Execute&& execute)
{
    if (transaction.Statements().empty() || !execute("START TRANSACTION"))
        return false;
    for (auto const& sql : transaction.Statements())
        if (!execute(sql))
        {
            execute("ROLLBACK");
            return false;
        }
    if (!execute("COMMIT"))
    {
        execute("ROLLBACK");
        return false;
    }
    return true;
}

template<class ConnectionInfo>
bool HRValidPrivateDatabaseTransport(ConnectionInfo const& connection)
{
    if (connection.host.empty() || connection.port_or_socket.empty() || connection.user.empty())
        return false;
    if (connection.host == ".")
        return true; // Unix socket or Windows named pipe, handled by the core.
    unsigned port = 0;
    for (unsigned char character : connection.port_or_socket)
    {
        if (character < '0' || character > '9')
            return false;
        port = port * 10 + (character - '0');
        if (port > 65535)
            return false;
    }
    return true;
}

// Keep authentication and transport settings in an independent copy. In
// particular, never change the live core pool's selected database or host.
template<class ConnectionInfo>
std::unique_ptr<ConnectionInfo> HRCopyPrivateDatabaseConnection(
    ConnectionInfo const* source, std::string const& databaseName)
{
    if (!source)
        return nullptr;
    auto connection = std::make_unique<ConnectionInfo>(*source);
    connection->database = databaseName;
#ifndef _WIN32
    // MySQLConnection::Open normalizes the Unix socket marker from "." to
    // "localhost" in the pool's shared info. Restore it only in our copy;
    // otherwise the next Open would parse the socket path as a TCP port.
    bool numericPort = std::all_of(connection->port_or_socket.begin(), connection->port_or_socket.end(),
        [](unsigned char character) { return character >= '0' && character <= '9'; });
    if (connection->host == "localhost" && !connection->port_or_socket.empty() && !numericPort)
        connection->host = ".";
#endif
    return connection;
}

#endif
