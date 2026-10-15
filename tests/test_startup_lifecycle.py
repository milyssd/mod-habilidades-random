"""Run production startup/login callbacks against deterministic core doubles.

The C++ bodies come from the module itself.  Only the surrounding AzerothCore
services are replaced, so this catches callback ordering and side effects rather
than testing a second implementation of the intended state machine.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def production_block(filename, declaration, *, trailing_semicolon=False):
    source = (ROOT / filename).read_text(encoding='utf-8')
    start = source.index(declaration)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if trailing_semicolon:
        assert source[end] == ';'
        end += 1
    return source[start:end]


def run_cpp(fixture, production, scenarios, cases):
    with tempfile.TemporaryDirectory(prefix='hr startup ') as directory:
        source = Path(directory) / 'startup.cpp'
        executable = Path(directory) / 'startup'
        source.write_text(fixture + '\n' + production + '\n' + scenarios, encoding='utf-8')
        subprocess.run([
            'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-I', str(ROOT / 'src'), str(source), '-o', str(executable),
        ], check=True)
        for case in cases:
            subprocess.run([str(executable), case], check=True)


WORLD_FIXTURE = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
std::vector<std::string> calls;
unsigned stopped = 0;
constexpr int ERROR_EXIT_CODE = 1;
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)

class WorldScript
{
public:
    explicit WorldScript(char const*) { }
    virtual void OnAfterConfigLoad(bool) { }
    virtual void OnBeforeWorldInitialized() { }
    virtual void OnStartup() { }
    virtual void OnUpdate(uint32) { }
    virtual void OnAfterUnloadAllMaps() { }
};
struct World { static void StopNow(int) { ++stopped; } };

struct Config
{
    bool enabled = true;
    template<class T> T GetOption(std::string const& name, T fallback)
    {
        if constexpr (std::is_same<T, bool>::value)
            if (name == "HabilidadesRandom.Enable")
                return enabled;
        return fallback;
    }
} config;
Config* sConfigMgr = &config;

struct Database
{
    bool opens = true;
    bool opened = false;
    bool Open(std::string const&, std::string const& = "")
    {
        calls.push_back("db.open");
        opened = opens;
        return opened;
    }
    void KeepAlive() { calls.push_back("db.keepalive"); }
    void Close() { calls.push_back("db.close"); opened = false; }
    bool IsOpen() const { return opened; }
} database;
#define HRDatabaseStore database

namespace HRRuntime
{
    bool profilesPrepare = true;
    bool prepares = true;
    bool PrepareProfiles() { calls.push_back("runtime.profiles"); return profilesPrepare; }
    bool Prepare() { calls.push_back("runtime.prepare"); return prepares; }
    void LoadNpcs() { calls.push_back("runtime.npcs"); }
}
void HRCloseCharacterSave() { calls.push_back("save.close"); }

struct Manager
{
    bool initialized = false;
    bool poolReady = true;
    bool storageReady = false, profilesReady = false, npcReady = false;
    void LoadConfig(bool = false) { calls.push_back("manager.config"); }
    void SetAvailability(bool storage, bool profiles, bool npc)
    {
        storageReady = storage;
        profilesReady = profiles;
        npcReady = npc;
    }
    bool Initialize()
    {
        calls.push_back("manager.initialize");
        initialized = true;
        return true;
    }
    bool CanRecover() const { return storageReady && profilesReady && initialized; }
    bool IsEnabled() const { return config.enabled && CanRecover() && npcReady && poolReady; }
} manager;
#define sHabilidadesRandomMgr (&manager)
'''


WORLD_SCENARIOS = r'''
#include <algorithm>
#include <iostream>
unsigned Count(std::string const& call)
{
    return std::count(calls.begin(), calls.end(), call);
}
std::size_t Position(std::string const& call)
{
    return std::find(calls.begin(), calls.end(), call) - calls.begin();
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    std::string scenario = argv[1];
    if (scenario == "disabled" || scenario == "enable_after_disabled")
        config.enabled = false;
    else if (scenario == "database_failure")
        database.opens = false;
    else if (scenario == "profile_failure")
        HRRuntime::profilesPrepare = false;
    else if (scenario == "npc_failure")
        HRRuntime::prepares = false;
    else if (scenario == "pool_failure")
        manager.poolReady = false;

    HRWorld world;
    world.OnAfterConfigLoad(false);
    world.OnBeforeWorldInitialized();
    world.OnStartup();
    assert(stopped == 0);
    if (scenario == "disabled" || scenario == "enable_after_disabled")
    {
        assert(Count("db.open") == 0);
        assert(Count("runtime.prepare") == 0);
        assert(Count("manager.initialize") == 0);
        assert(Count("runtime.npcs") == 0);
        assert(!manager.CanRecover() && !manager.IsEnabled());
        world.OnUpdate(120000);
        assert(Count("db.keepalive") == 0);
        if (scenario == "enable_after_disabled")
        {
            config.enabled = true;
            world.OnAfterConfigLoad(true);
            world.OnUpdate(120000);
            assert(Count("db.open") == 0);
            assert(Count("runtime.npcs") == 0);
            assert(!manager.IsEnabled()); // A cold enable needs a restart.
        }
    }
    else if (scenario == "database_failure" || scenario == "profile_failure")
    {
        assert(Count("runtime.npcs") == 0);
        assert(Count("manager.initialize") == 0);
        assert(!manager.CanRecover() && !manager.IsEnabled());
        if (scenario == "database_failure")
        {
            world.OnUpdate(60000);
            assert(Count("db.keepalive") == 0);
        }
    }
    else if (scenario == "npc_failure" || scenario == "pool_failure")
    {
        assert(Count("manager.initialize") == 1);
        assert(Count("runtime.npcs") == 0);
        assert(manager.CanRecover()); // Original powers can still be restored.
        assert(!manager.IsEnabled());
        world.OnUpdate(60000);
        assert(Count("db.keepalive") == 1);
    }
    else
    {
        assert(scenario == "success" || scenario == "disable_after_enabled");
        assert(Count("db.open") == 1);
        assert(Count("runtime.prepare") == 1);
        assert(Count("manager.initialize") == 1);
        assert(Count("runtime.npcs") == 1);
        assert(manager.CanRecover() && manager.IsEnabled());
        assert(Position("runtime.profiles") < Position("db.open"));
        assert(Position("db.open") < Position("runtime.prepare"));
        assert(Position("runtime.prepare") < Position("manager.initialize"));
        assert(Position("manager.initialize") < Position("runtime.npcs"));
        world.OnUpdate(59999);
        assert(Count("db.keepalive") == 0);
        world.OnUpdate(1);
        assert(Count("db.keepalive") == 1);
        world.OnUpdate(59999);
        assert(Count("db.keepalive") == 1);
        world.OnUpdate(1);
        assert(Count("db.keepalive") == 2);
        if (scenario == "disable_after_enabled")
        {
            config.enabled = false;
            world.OnAfterConfigLoad(true);
            assert(!manager.IsEnabled());
            assert(manager.CanRecover());
            assert(database.opened); // Keep recovery for existing participants.
        }
    }
    world.OnAfterUnloadAllMaps();
    world.OnAfterUnloadAllMaps();
    assert(!database.opened);
    assert(Count("save.close") == 2 && Count("db.close") == 2);
    world.OnUpdate(120000);
    assert(stopped == 0);
    std::cout << "PASS " << scenario << '\n';
}
'''


LOGIN_FIXTURE = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "HRDraftState.h"

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
constexpr uint8 CLASS_DRUID = 11;
std::vector<std::string> calls;
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)

struct Session
{
    bool kicked = false;
    void KickPlayer(std::string const&) { calls.push_back("player.kick"); kicked = true; }
};
struct Player
{
    Session session;
    bool saveSuppressed = false;
    uint8 classId = 1, race = 1, level = 1;
    Session* GetSession() { return &session; }
    uint8 getClass() const { return classId; }
    uint8 getRace() const { return race; }
    uint8 GetLevel() const { return level; }
};
uint32 Guid(Player const*) { return 7; }

struct Field
{
    uint32 count;
    template<class T> T Get() const { return static_cast<T>(count); }
};
struct Result
{
    Field field;
    Field* Fetch() { return &field; }
};
struct Database
{
    bool opened = false;
    bool querySucceeds = true;
    uint32 matches = 0;
    bool IsOpen() const { return opened; }
    std::shared_ptr<Result> Query(std::string const&)
    {
        calls.push_back("db.query");
        return querySucceeds ? std::make_shared<Result>(Result{{matches}}) : nullptr;
    }
} database;
#define HRDatabaseStore database

namespace HRRuntime
{
    bool eventOnlyRace = false;
    bool IsEventOnlyDruidRace(uint8) { return eventOnlyRace; }
}

struct State
{
    bool completed = false;
    bool characterPending = false;
    uint8 originalClass = 1;
    HRStats stats;
};
struct HREventRegistration
{
    bool completed = false;
    bool characterPending = false;
    uint8 originalClass = 0;
};
class HabilidadesRandomMgr
{
public:
    mutable std::recursive_mutex _mutex;
    std::unordered_map<uint32, State> _players;
    std::unordered_map<uint32, HREventRegistration> _registrations;
    std::unordered_set<uint32> _recoveryRequired;
    bool _enabled = true, _ready = false;
    bool _storageReady = false, _profilesReady = false, _npcReady = false, _registryReady = false;
    bool loadSucceeds = true;
    State stored;
    void SetAvailability(bool storageReady, bool profilesReady, bool npcReady);
    bool CanRecover() const;
    /* PRODUCTION_ENABLED */
    void RequireRecovery(Player* player)
    {
        _recoveryRequired.insert(Guid(player));
        player->saveSuppressed = true;
        player->GetSession()->KickPlayer("requires recovery");
    }
    bool LoadPlayer(Player* player)
    {
        calls.push_back("manager.load");
        if (loadSucceeds)
            _players[Guid(player)] = stored;
        return loadSucceeds;
    }
    bool Restore(Player*) { calls.push_back("manager.restore"); return true; }
    void Suspend(Player*) { calls.push_back("manager.suspend"); }
    bool Save(Player*, State&, bool) { calls.push_back("manager.save"); return true; }
    void Check(Player*) { calls.push_back("manager.check"); }
    void OnLevelChanged(Player*, uint8) { calls.push_back("manager.level"); }
    void OnLogin(Player* player);
};
'''


LOGIN_SCENARIOS = r'''
#include <algorithm>
#include <iostream>
unsigned Count(std::string const& call)
{
    return std::count(calls.begin(), calls.end(), call);
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    std::string scenario = argv[1];
    Player player;
    HabilidadesRandomMgr manager;
    manager._ready = true;
    manager._registryReady = true;
    manager.SetAvailability(true, true, true);
    manager.stored.stats.lastLevel = player.level;
    database.opened = true;
    database.matches = 1;
    bool unrelated = scenario.rfind("unrelated_", 0) == 0;
    bool synthetic = scenario.rfind("synthetic_", 0) == 0;
    if (!unrelated && scenario != "synthetic_cold")
        manager._registrations[Guid(&player)] = {false, false, 1};
    if (unrelated)
        database.querySucceeds = false; // Unrelated logins must never touch SQL.
    if (synthetic)
    {
        player.classId = CLASS_DRUID;
        HRRuntime::eventOnlyRace = true;
    }
    if (scenario == "unrelated_cold" || scenario == "synthetic_cold" ||
        scenario == "registered_no_storage" || scenario == "completed_no_storage" ||
        scenario == "storage_returned")
    {
        manager.SetAvailability(false, true, false);
        manager._enabled = false;
        database.opened = false;
    }
    if (scenario == "registered_no_profiles")
        manager.SetAvailability(true, false, true);
    if (scenario == "unrelated_index_failed" || scenario == "registered_index_failed")
        manager._registryReady = false;
    if (scenario == "registered_outage")
        database.querySucceeds = false;
    if (scenario == "registered_incomplete")
        manager.loadSucceeds = false;
    if (scenario == "registered_missing" || scenario == "synthetic_missing")
    {
        database.matches = 0;
        manager._players[Guid(&player)] = {};
    }
    if (scenario.rfind("completed_", 0) == 0)
    {
        manager._registrations[Guid(&player)].completed = true;
        manager.stored.completed = true;
        if (scenario != "completed_no_storage")
            player.classId = CLASS_DRUID;
        if (scenario == "completed_pending")
        {
            manager._registrations[Guid(&player)].characterPending = true;
            manager.stored.characterPending = true;
        }
    }
    if (scenario == "active_disabled")
        manager._enabled = false;
    if (scenario == "active_pool_failure")
        manager._ready = false;
    if (scenario == "active_npc_failure")
        manager.SetAvailability(true, true, false);
    if (scenario == "active_level80")
        player.level = 80;
    if (scenario == "active_level_replay")
        player.level = 12;

    manager.OnLogin(&player);
    if (unrelated || scenario == "completed_no_storage")
    {
        assert(calls.empty());
        assert(!player.session.kicked && !player.saveSuppressed);
    }
    else if (scenario == "registered_missing")
    {
        assert(Count("db.query") == 1);
        assert(Count("manager.load") == 0);
        assert(!manager._registrations.count(Guid(&player)));
        assert(!manager._players.count(Guid(&player)));
        assert(!player.session.kicked && !player.saveSuppressed);
    }
    else if (scenario == "active_normal" || scenario == "active_level_replay")
    {
        assert(Count("db.query") == 1 && Count("manager.load") == 1);
        assert(Count("manager.suspend") == 1 && Count("manager.restore") == 0);
        assert(!player.session.kicked);
        if (scenario == "active_normal")
            assert(Count("manager.save") == 1 && Count("manager.check") == 1);
        else
            assert(Count("manager.level") == 1 && Count("manager.save") == 0);
    }
    else if (scenario.rfind("active_", 0) == 0 || scenario == "completed_pending" ||
             scenario == "completed_mismatch")
    {
        assert(Count("db.query") == 1 && Count("manager.load") == 1);
        assert(Count("manager.restore") == 1 && Count("manager.suspend") == 0);
        assert(!player.session.kicked);
    }
    else
    {
        assert(player.session.kicked && player.saveSuppressed);
        assert(manager._recoveryRequired.count(Guid(&player)));
        assert(Count("manager.restore") == 0 && Count("manager.suspend") == 0);
        if (scenario == "registered_no_storage" || scenario == "registered_no_profiles" ||
            scenario == "registered_index_failed" || scenario == "synthetic_cold" ||
            scenario == "storage_returned")
            assert(Count("db.query") == 0);
        else
            assert(Count("db.query") == 1);
    }
    if (scenario == "storage_returned")
    {
        calls.clear();
        player.session.kicked = false;
        player.saveSuppressed = false; // The reconnect constructs a fresh core Player.
        manager._enabled = true;
        manager.SetAvailability(true, true, true);
        database.opened = true;
        manager.OnLogin(&player);
        assert(!player.session.kicked && !manager._recoveryRequired.count(Guid(&player)));
        assert(Count("db.query") == 1 && Count("manager.load") == 1);
        assert(Count("manager.suspend") == 1 && Count("manager.save") == 1);
    }
    std::cout << "PASS " << scenario << '\n';
}
'''


INITIALIZE_FIXTURE = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
constexpr uint8 HR_BROKEN = 5;
std::vector<std::string> calls;
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)

struct Field
{
    uint64 value;
    template<class T> T Get() const { return static_cast<T>(value); }
};
struct Result
{
    std::vector<std::array<Field, 4>> rows;
    std::size_t index = 0;
    Field* Fetch() { return rows[index].data(); }
    bool NextRow() { return ++index < rows.size(); }
};
using QueryResult = std::shared_ptr<Result>;
struct Database
{
    uint64 expected = 2;
    bool countWorks = true, schemaWorks = true, poolWorks = true;
    std::vector<std::array<Field, 4>> registrations = {
        {{{7}, {false}, {false}, {1}}},
        {{{8}, {true}, {true}, {4}}},
    };
    QueryResult Query(std::string const& sql)
    {
        if (sql.find("information_schema") != std::string::npos)
        {
            calls.push_back("schema");
            return schemaWorks ? std::make_shared<Result>(Result{{{{{{6}, {0}, {0}, {0}}}}}}) : nullptr;
        }
        if (sql.find("SELECT COUNT(*)") == 0)
        {
            calls.push_back("count");
            return countWorks ? std::make_shared<Result>(Result{{{{{{expected}, {0}, {0}, {0}}}}}}) : nullptr;
        }
        if (sql.find("SELECT player_id") == 0)
        {
            calls.push_back("registrations");
            return registrations.empty() ? nullptr : std::make_shared<Result>(Result{registrations});
        }
        assert(sql.find("SELECT spell_id,rarity") == 0);
        calls.push_back("pool");
        return poolWorks ? std::make_shared<Result>(Result{{{{{{123}, {2}, {0}, {0}}}}}}) : nullptr;
    }
} database;
#define HRDatabaseStore database
struct SpellMgr
{
    bool valid = true;
    SpellMgr* GetSpellInfo(uint32) { return valid ? this : nullptr; }
} spellMgr;
#define sSpellMgr (&spellMgr)

struct HREventRegistration
{
    bool completed = false;
    bool characterPending = false;
    uint8 originalClass = 0;
};
class HabilidadesRandomMgr
{
public:
    mutable std::recursive_mutex _mutex;
    bool _storageReady = true, _profilesReady = true;
    bool _registryReady = true, _ready = true;
    std::unordered_map<uint32, HREventRegistration> _registrations = {{99, {false, false, 1}}};
    std::unordered_map<uint32, uint8> _rarities = {{456, 3}};
    std::unordered_map<uint32, uint32> _players = {{99, 1000}};
    uint32 Root(uint32 id) const { return id; }
    bool IsProtected(uint32) const { return false; }
    void Initialize();
};
'''


INITIALIZE_SCENARIOS = r'''
#include <algorithm>
#include <iostream>
unsigned Count(std::string const& call)
{
    return std::count(calls.begin(), calls.end(), call);
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    std::string scenario = argv[1];
    HabilidadesRandomMgr manager;
    if (scenario == "no_storage")
        manager._storageReady = false;
    else if (scenario == "no_profiles")
        manager._profilesReady = false;
    else if (scenario == "count_failure")
        database.countWorks = false;
    else if (scenario == "partial_registry")
        database.registrations.pop_back();
    else if (scenario == "missing_registry")
        database.registrations.clear();
    else if (scenario == "unexpected_registry")
        database.expected = 0;
    else if (scenario == "empty_registry")
    {
        database.expected = 0;
        database.registrations.clear();
    }
    else if (scenario == "schema_failure")
        database.schemaWorks = false;
    else if (scenario == "empty_pool")
        database.poolWorks = false;
    else if (scenario == "invalid_pool")
        spellMgr.valid = false;

    manager.Initialize();
    assert(manager._players.size() == 1 && manager._players.at(99) == 1000);
    assert(!manager._rarities.count(456)); // Do not reuse a stale pool after failure.
    if (scenario == "no_storage" || scenario == "no_profiles")
    {
        assert(calls.empty());
        assert(!manager._ready);
    }
    else if (scenario == "count_failure" || scenario == "partial_registry" ||
             scenario == "missing_registry" || scenario == "unexpected_registry")
    {
        assert(!manager._registryReady && !manager._ready);
        assert(manager._registrations.size() == 1 && manager._registrations.count(99));
        assert(Count("schema") == 0 && Count("pool") == 0);
    }
    else
    {
        assert(manager._registryReady);
        assert(!manager._registrations.count(99));
        if (scenario == "empty_registry")
            assert(manager._registrations.empty());
        else
        {
            assert(manager._registrations.size() == 2);
            assert(!manager._registrations.at(7).completed);
            assert(manager._registrations.at(7).originalClass == 1);
            assert(manager._registrations.at(8).completed);
            assert(manager._registrations.at(8).characterPending);
            assert(manager._registrations.at(8).originalClass == 4);
        }
        if (scenario == "schema_failure" || scenario == "empty_pool" || scenario == "invalid_pool")
            assert(!manager._ready && manager._rarities.empty());
        else
            assert(manager._ready && manager._rarities.at(123) == 2);
    }
    std::cout << "PASS " << scenario << '\n';
}
'''


@unittest.skipUnless(shutil.which('g++'), 'G++ is required for startup regressions')
class StartupLifecycleTests(unittest.TestCase):
    def test_real_world_callbacks_do_not_stop_the_server(self):
        world = production_block('src/HabilidadesRandomScripts.cpp',
                                 'class HRWorld : public WorldScript', trailing_semicolon=True)
        run_cpp(WORLD_FIXTURE, world, WORLD_SCENARIOS, (
            'disabled', 'database_failure', 'profile_failure', 'npc_failure',
            'pool_failure', 'success', 'enable_after_disabled', 'disable_after_enabled',
        ))

    def test_real_login_isolates_only_event_participants_and_keeps_recovery(self):
        enabled = production_block('src/HabilidadesRandom.h', 'bool IsEnabled() const')
        fixture = LOGIN_FIXTURE.replace('/* PRODUCTION_ENABLED */', enabled)
        methods = '\n'.join(production_block('src/HabilidadesRandom.cpp', declaration) for declaration in (
            'void HabilidadesRandomMgr::SetAvailability(',
            'bool HabilidadesRandomMgr::CanRecover() const',
            'void HabilidadesRandomMgr::OnLogin(Player* player)',
        ))
        run_cpp(fixture, methods, LOGIN_SCENARIOS, (
            'unrelated_cold', 'unrelated_outage', 'unrelated_index_failed',
            'registered_no_storage', 'registered_no_profiles', 'registered_index_failed',
            'registered_outage', 'registered_incomplete', 'registered_missing',
            'synthetic_cold', 'synthetic_missing', 'completed_no_storage',
            'completed_pending', 'completed_mismatch', 'active_disabled',
            'active_pool_failure', 'active_npc_failure', 'active_level80',
            'active_normal', 'active_level_replay', 'storage_returned',
        ))

    def test_real_initialize_requires_a_complete_registry_but_not_a_working_pool_for_recovery(self):
        initialize = production_block('src/HabilidadesRandom.cpp', 'void HabilidadesRandomMgr::Initialize()')
        run_cpp(INITIALIZE_FIXTURE, initialize, INITIALIZE_SCENARIOS, (
            'no_storage', 'no_profiles', 'count_failure', 'partial_registry',
            'missing_registry', 'unexpected_registry', 'empty_registry',
            'schema_failure', 'empty_pool', 'invalid_pool', 'success',
        ))


if __name__ == '__main__':
    unittest.main()
