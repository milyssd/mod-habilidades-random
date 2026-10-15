#include "../src/HRDatabaseConnection.h"
#include <cassert>
#include <string>

struct ConnectionInfo
{
    std::string host;
    std::string port_or_socket;
    std::string user;
    std::string password;
    std::string database;
    std::string ssl;
    // Copy future connection properties too; do not rebuild a connection string.
    std::string extra;
};

static bool Same(ConnectionInfo const& first, ConnectionInfo const& second)
{
    return first.host == second.host && first.port_or_socket == second.port_or_socket &&
        first.user == second.user && first.password == second.password && first.database == second.database &&
        first.ssl == second.ssl && first.extra == second.extra;
}

static void CheckTransport(std::string const& host, std::string const& port, std::string const& expectedHost)
{
    ConnectionInfo source{host, port, "synthetic_account", "synthetic_password", "characters", "ssl", "preserve_me"};
    ConnectionInfo original = source;
    auto copy = HRCopyPrivateDatabaseConnection(&source, "mod_habilidades_random");
    assert(copy && copy.get() != &source);
    assert(copy->host == expectedHost);
    assert(copy->port_or_socket == original.port_or_socket);
    assert(copy->database == "mod_habilidades_random");
    assert(copy->user == original.user && copy->password == original.password && copy->ssl == original.ssl);
    assert(copy->extra == original.extra);
    assert(Same(source, original));
    copy->host = "changed_copy";
    copy->port_or_socket = "9999";
    copy->user.clear();
    copy->password.clear();
    copy->database.clear();
    copy->ssl.clear();
    copy->extra.clear();
    assert(Same(source, original));
}

int main()
{
    assert(!HRCopyPrivateDatabaseConnection(static_cast<ConnectionInfo const*>(nullptr), "event"));
    CheckTransport("127.0.0.1", "3306", "127.0.0.1");
    CheckTransport("localhost", "3306", "localhost");
    CheckTransport("localhost", "0", "localhost");
    CheckTransport("localhost", "", "localhost");
    CheckTransport(".", "/tmp/synthetic-mysql.sock", ".");
    CheckTransport(".", "synthetic_named_pipe", ".");
    CheckTransport("remote.example.invalid", "/tmp/synthetic-mysql.sock", "remote.example.invalid");
    CheckTransport("127.0.0.1", "non_numeric", "127.0.0.1");
#ifdef _WIN32
    CheckTransport("localhost", "/tmp/synthetic-mysql.sock", "localhost");
    CheckTransport("localhost", std::string(1, static_cast<char>(0xE1)), "localhost");
#else
    CheckTransport("localhost", "/tmp/synthetic-mysql.sock", ".");
    // Non-ASCII bytes are never decimal digits, regardless of locale or char signedness.
    CheckTransport("localhost", std::string(1, static_cast<char>(0xE1)), ".");
#endif
    ConnectionInfo noPassword{"localhost", "3306", "synthetic_account", "", "characters", "", "extra"};
    auto copy = HRCopyPrivateDatabaseConnection(&noPassword, "event");
    assert(copy && copy->password.empty() && copy->ssl.empty());
    assert(noPassword.database == "characters");
    noPassword.user = "changed_source";
    assert(copy->user == "synthetic_account");
}
