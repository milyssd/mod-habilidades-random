#ifndef HR_DATABASE_POLICY_H
#define HR_DATABASE_POLICY_H
#include <algorithm>
#include <string>
#include <vector>

inline std::string HRLowerDatabaseName(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
    {
        return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : char(c);
    });
    return value;
}

inline bool HRValidPrivateDatabaseName(std::string const& name, std::vector<std::string> const& coreDatabases)
{
    if (name.empty() || name.size() > 64 || !std::all_of(name.begin(), name.end(), [](unsigned char c)
        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }))
        return false;
    std::string lower = HRLowerDatabaseName(name);
    for (auto const& core : coreDatabases)
        if (lower == HRLowerDatabaseName(core))
            return false;
    for (char const* reserved : {"mysql", "sys", "information_schema", "performance_schema"})
        if (lower == reserved)
            return false;
    return true;
}
#endif
