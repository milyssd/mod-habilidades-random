// Compile the production runtime against deterministic map/database doubles.
// g++ -std=c++17 -Itests/runtime_stubs tests/test_runtime.cpp -o /tmp/test_hr_runtime
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include "runtime_stubs/RuntimeTestCore.h"

struct TestField
{
    double value;
    template<typename T> T Get() const { return static_cast<T>(value); }
};
struct TestResult
{
    std::vector<std::vector<TestField>> rows;
    std::size_t position = 0;
    TestField* Fetch() { return rows[position].data(); }
    bool NextRow() { return ++position < rows.size(); }
};
class TestDatabase
{
public:
    bool open = true, failDelete = false;
    uint32 nextId = 0;
    std::map<uint32, std::vector<TestField>> rows;
    bool IsOpen() const { return open; }
    bool ExecuteInsert(std::string const& sql, uint32& insertedId)
    {
        insertedId = 0;
        if (!Execute(sql))
            return false;
        insertedId = nextId;
        return true;
    }
    bool Execute(std::string const& sql)
    {
        if (sql.rfind("INSERT INTO", 0) == 0)
        {
            uint32 mapId;
            float x, y, z, o;
            assert(std::sscanf(sql.c_str() + sql.rfind('('), "(%u,%f,%f,%f,%f)", &mapId, &x, &y, &z, &o) == 5);
            ++nextId;
            rows[nextId] = {{double(nextId)}, {double(mapId)}, {x}, {y}, {z}, {o}};
            return true;
        }
        assert(sql.rfind("DELETE FROM", 0) == 0);
        if (failDelete)
            return false;
        rows.erase(uint32(std::stoul(sql.substr(sql.rfind('=') + 1))));
        return true;
    }
    std::shared_ptr<TestResult> Query(std::string const& sql)
    {
        auto result = std::make_shared<TestResult>();
        if (sql == "SELECT LAST_INSERT_ID()")
            result->rows = {{{double(nextId)}}};
        else
            for (auto const& row : rows)
                result->rows.push_back(row.second);
        return result->rows.empty() ? nullptr : result;
    }
};
inline TestDatabase TestDb;
// Replace the private connection only; compile all production spawn/removal/registration logic.
#define HR_DATABASE_H
#define HRDatabaseStore TestDb
#include "../src/HRRuntime.cpp"

int main()
{
    std::array<PlayerLevelInfo, 2> baseline;
    for (uint32 race : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 11u})
    {
        PlayerInfo profile;
        profile.levelInfo = baseline.data();
        TestObjectMgr.profiles[{race, 1}] = profile;
        if (race == 1 || race == 4 || race == 6)
            TestObjectMgr.profiles[{race, CLASS_DRUID}] = profile;
    }
    TestObjectMgr.creatures[68].Entry = 68;
    assert(HRRuntime::Prepare());
    assert(HRRuntime::Prepare()); // Initialization is idempotent.
    assert(!HRRuntime::IsEventOnlyDruidRace(1)); // Existing custom/native profiles remain available.
    assert(!HRRuntime::IsEventOnlyDruidRace(4));
    assert(HRRuntime::IsEventOnlyDruidRace(2));
    auto const* npc = TestObjectMgr.GetCreatureTemplate(900100);
    assert(npc && (npc->unit_flags & UNIT_FLAG_IMMUNE_TO_PC) && (npc->unit_flags & UNIT_FLAG_IMMUNE_TO_NPC));

    Player player;
    player.x = 123.25f;
    assert(!HRRuntime::PlaceNpc(nullptr));
    assert(HRRuntime::PlaceNpc(&player));
    player.mapId = 1;
    assert(HRRuntime::PlaceNpc(&player));
    auto* first = TestMapMgr.maps[0].creatures.back().get();
    auto* second = TestMapMgr.maps[1].creatures.back().get();
    assert(first->GetGUID() == second->GetGUID()); // Reproduces the actual core's cross-map collision.
    assert(first->active && second->active && HRRuntime::IsNpc(first));
    assert(HRRuntime::RemoveNpc(first));
    assert(first->despawned && !second->despawned);
    assert(!TestDb.rows.count(1) && TestDb.rows.count(2));
    TestDb.failDelete = true;
    assert(!HRRuntime::RemoveNpc(second));
    assert(TestDb.rows.count(2) && !second->despawned);
    TestDb.failDelete = false;
    assert(HRRuntime::RemoveNpc(second));
    assert(!HRRuntime::RemoveNpc(second));

    player.mapId = 530;
    TestMapMgr.CreateBaseMap(530)->failSpawn = true;
    assert(!HRRuntime::PlaceNpc(&player));
    assert(TestDb.rows.empty()); // A failed spawn rolls back its private row.
    player.mapId = 33;
    assert(!HRRuntime::PlaceNpc(&player));
    player.mapId = 0;
    player.onTransport = true;
    assert(!HRRuntime::PlaceNpc(&player));
    player.onTransport = false;
    assert(HRRuntime::PlaceNpc(&player));
    // Reload the private DB rows into fresh maps, as at server startup.
    Spawns.clear();
    TestMapMgr.maps.clear();
    HRRuntime::LoadNpcs();
    auto* restored = TestMapMgr.maps[0].creatures.back().get();
    assert(restored->position.x == 123.25f && restored->active);
    assert(HRRuntime::RemoveNpc(restored));
    assert(TestDb.rows.empty());
    std::cout << "PASS: runtime profiles, map-local GUIDs, private NPC persistence/removal and failed-spawn rollback\n";
}
