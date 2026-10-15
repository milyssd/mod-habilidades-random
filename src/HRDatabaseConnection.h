#ifndef HR_DATABASE_CONNECTION_H
#define HR_DATABASE_CONNECTION_H

#include <algorithm>
#include <memory>
#include <string>

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
