#ifndef HR_RUNTIME_TEST_CORE_H
#define HR_RUNTIME_TEST_CORE_H
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using int64 = std::int64_t;
constexpr uint32 CLASS_DRUID = 11, CLASS_DEATH_KNIGHT = 6, MAX_CLASSES = 12, MAX_STATS = 5;
constexpr uint32 CONFIG_MAX_PLAYER_LEVEL = 0, UNIT_NPC_FLAG_GOSSIP = 1;
constexpr uint32 CREATURE_FLAG_EXTRA_CIVILIAN = 1, CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT = 2;
constexpr uint32 UNIT_FLAG_IMMUNE_TO_PC = 256, UNIT_FLAG_IMMUNE_TO_NPC = 512;
#ifndef HR_TEST_NO_RUNTIME_API
#define ACORE_RUNTIME_MODULE_DATA 1
#endif
#ifndef HR_TEST_NO_SAVE_GUARD
#define ACORE_MODULE_SAVE_GUARD 1
#endif
inline uint32 TestErrors = 0;
#define LOG_ERROR(...) (++TestErrors)

struct ObjectGuid
{
    std::uint64_t value = 0;
    bool operator<(ObjectGuid const& other) const { return value < other.value; }
    bool operator==(ObjectGuid const& other) const { return value == other.value; }
};

struct Position
{
    float x = 0, y = 0, z = 0, o = 0;
    Position() = default;
    Position(float x_, float y_, float z_, float o_) : x(x_), y(y_), z(z_), o(o_) { }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    float GetOrientation() const { return o; }
};

class Player : public Position
{
public:
    uint32 mapId = 0;
    bool onTransport = false;
    uint32 GetMapId() const { return mapId; }
    bool GetTransport() const { return onTransport; }
    Position const& GetPosition() const { return *this; }
};

class Creature
{
public:
    virtual ~Creature() = default;
    ObjectGuid guid;
    uint32 entry = 0, mapId = 0;
    bool despawned = false;
    ObjectGuid GetGUID() const { return guid; }
    uint32 GetMapId() const { return mapId; }
    uint32 GetEntry() const { return entry; }
    void DespawnOrUnsummon() { despawned = true; }
};

class TempSummon : public Creature
{
public:
    bool active = false;
    Position position;
    void setActive(bool value) { active = value; }
};

class Map
{
public:
    uint32 id = 0;
    std::uint64_t nextGuid = 0;
    bool failSpawn = false;
    std::vector<std::unique_ptr<TempSummon>> creatures;
    TempSummon* SummonCreature(uint32 entry, Position const& position)
    {
        if (failSpawn)
            return nullptr;
        auto creature = std::make_unique<TempSummon>();
        creature->guid.value = ++nextGuid; // Actual core allocates creature GUIDs separately on each map.
        creature->entry = entry;
        creature->mapId = id;
        creature->position = position;
        TempSummon* result = creature.get();
        creatures.push_back(std::move(creature));
        return result;
    }
};

class MapMgr
{
public:
    std::map<uint32, Map> maps;
    static bool IsValidMapCoord(uint32, float x, float y, float z, float o)
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(o);
    }
    Map* CreateBaseMap(uint32 id)
    {
        Map& map = maps[id];
        map.id = id;
        return &map;
    }
};

struct PlayerLevelInfo { uint32 stats[MAX_STATS] = {10, 10, 10, 10, 10}; };
struct PlayerInfo
{
    uint32 mapId = 0, areaId = 0, displayId_m = 1, displayId_f = 2;
    float positionX = 0, positionY = 0, positionZ = 0, orientation = 0;
    PlayerLevelInfo* levelInfo = nullptr;
};
struct CreatureTemplate
{
    uint32 Entry = 0, minlevel = 0, maxlevel = 0, faction = 0, npcflag = 0, unit_flags = 0;
    uint32 flags_extra = 0, ScriptID = 0, GossipMenuId = 0, lootid = 0, pickpocketLootId = 0, SkinLootId = 0;
    uint32 mingold = 0, maxgold = 0;
    uint32 DifficultyEntry[3] = {}, KillCredit[2] = {}, spells[8] = {};
    std::string Name, SubName, AIName;
};
class ObjectMgr
{
public:
    std::map<std::pair<uint32, uint32>, PlayerInfo> profiles;
    std::map<std::pair<uint32, uint32>, std::vector<PlayerLevelInfo>> levels;
    std::map<uint32, CreatureTemplate> creatures;
    PlayerInfo const* GetPlayerInfo(uint32 race, uint32 classId) const
    {
        auto found = profiles.find({race, classId});
        return found == profiles.end() ? nullptr : &found->second;
    }
    CreatureTemplate const* GetCreatureTemplate(uint32 entry) const
    {
        auto found = creatures.find(entry);
        return found == creatures.end() ? nullptr : &found->second;
    }
    bool RegisterRuntimePlayerInfo(uint32 race, uint32 classId, PlayerInfo const& source,
        std::vector<PlayerLevelInfo> const& data)
    {
        auto key = std::make_pair(race, classId);
        if (profiles.count(key))
            return false;
        levels[key] = data;
        profiles[key] = source;
        profiles[key].levelInfo = levels[key].data();
        return true;
    }
    bool RegisterRuntimeCreatureTemplate(CreatureTemplate const& source)
    {
        return creatures.emplace(source.Entry, source).second;
    }
};
class World { public: uint32 getIntConfig(uint32) const { return 2; } };
class ConfigMgr
{
public:
    template<typename T> T GetOption(std::string const&, T fallback) const { return fallback; }
};

inline MapMgr TestMapMgr;
inline ObjectMgr TestObjectMgr;
inline World TestWorld;
inline ConfigMgr TestConfigMgr;
#define sMapMgr (&TestMapMgr)
#define sObjectMgr (&TestObjectMgr)
#define sWorld (&TestWorld)
#define sConfigMgr (&TestConfigMgr)
#endif
