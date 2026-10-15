#include "../src/HRDatabaseSchema.h"
#include "../src/HRDatabasePolicy.h"
#include <cassert>
#include <set>
#include <iostream>

int main()
{
    std::vector<std::string> core = {"acore_world", "acore_characters", "acore_auth"};
    assert(HRValidPrivateDatabaseName("mod_habilidades_random", core));
    assert(HRValidPrivateDatabaseName("Private_Realm_2", core));
    for (auto name : {"", "acore_world", "ACORE_CHARACTERS", "acore_auth", "mysql", "SYS", "information_schema",
            "performance_schema", "bad`name", "bad;name", "with space", "slash/name"})
        assert(!HRValidPrivateDatabaseName(name, core));
    assert(!HRValidPrivateDatabaseName(std::string(65, 'a'), core));
    std::set<unsigned> ids;
    for (auto const& entry : HRSchema::Pool)
    {
        assert(entry.id && entry.rarity <= 5);
        assert(ids.insert(entry.id).second);
    }
    assert(ids.size() == 474);
    assert(std::size(HRSchema::Statements) == 10);
    assert(HRSchema::Version == 2);
    for (auto statement : HRSchema::Statements)
        assert(statement.find("ENGINE=InnoDB") != std::string_view::npos);
    std::cout << "PASS: private schema isolation, identifier validation, skill snapshot schema and 474 unique pool entries\n";
}
