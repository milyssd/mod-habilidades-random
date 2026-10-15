#include "HRRuntime.h"
#include "HRDatabase.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureData.h"
#include "DBCStores.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace
{
std::recursive_mutex RuntimeMutex;
uint32 NpcEntry = 900100;
bool Prepared = false;
// Creature GUID counters are local to each map: GUID alone is not a unique spawn key.
std::map<std::pair<uint32, ObjectGuid>, uint32> Spawns;
std::unordered_set<uint32> EventOnlyDruidRaces;

bool ValidMap(uint32 id)
{
    return id == 0 || id == 1 || id == 530 || id == 571;
}

bool Spawn(uint32 rowId, uint32 mapId, Position const& position)
{
    if (!ValidMap(mapId) || !MapMgr::IsValidMapCoord(mapId, position.GetPositionX(), position.GetPositionY(),
            position.GetPositionZ(), position.GetOrientation()))
        return false;
    Map* map = sMapMgr->CreateBaseMap(mapId);
    if (!map)
        return false;
    TempSummon* creature = map->SummonCreature(NpcEntry, position);
    if (!creature)
        return false;
    creature->setActive(true);
    Spawns[{mapId, creature->GetGUID()}] = rowId;
    return true;
}
}

bool HRRuntime::Prepare()
{
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex);
    if (Prepared)
        return true;
#if !defined(ACORE_RUNTIME_MODULE_DATA) || !defined(ACORE_MODULE_SAVE_GUARD)
    LOG_ERROR("server.loading", "HabilidadesRandom: ejecutar python modules/mod-habilidades-random/tools/prepare_core.py --core . desde la raíz del core y recompilar. No se escribirá en world.");
    return false;
#else
    // Register only absent pairs. Native druid data and every existing player profile stay untouched.
    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    for (uint32 race : {1u,2u,3u,4u,5u,6u,7u,8u,10u,11u})
    {
        if (sObjectMgr->GetPlayerInfo(race, CLASS_DRUID))
            continue;
        PlayerInfo const* target = nullptr;
        PlayerInfo const* reference = nullptr;
        PlayerInfo const* druid = nullptr;
        for (uint32 native : {4u,6u})
        {
            for (uint32 sharedClass = 1; sharedClass < MAX_CLASSES; ++sharedClass)
            {
                // Heroic class starting locations are unsuitable as fallback homebinds.
                if (sharedClass == CLASS_DEATH_KNIGHT)
                    continue;
                target = sObjectMgr->GetPlayerInfo(race, sharedClass);
                reference = sObjectMgr->GetPlayerInfo(native, sharedClass);
                druid = sObjectMgr->GetPlayerInfo(native, CLASS_DRUID);
                if (target && reference && druid && target->levelInfo && reference->levelInfo && druid->levelInfo)
                    break;
                target = nullptr;
            }
            if (target)
                break;
        }
        if (!target)
            return false;
        PlayerInfo synthetic;
        synthetic.mapId = target->mapId;
        synthetic.areaId = target->areaId;
        synthetic.positionX = target->positionX;
        synthetic.positionY = target->positionY;
        synthetic.positionZ = target->positionZ;
        synthetic.orientation = target->orientation;
        synthetic.displayId_m = target->displayId_m;
        synthetic.displayId_f = target->displayId_f;
        std::vector<PlayerLevelInfo> levels(maxLevel);
        for (uint32 level = 0; level < maxLevel; ++level)
            for (uint8 stat = 0; stat < MAX_STATS; ++stat)
            {
                int64 value = int64(druid->levelInfo[level].stats[stat]) +
                    int64(target->levelInfo[level].stats[stat]) - int64(reference->levelInfo[level].stats[stat]);
                levels[level].stats[stat] = uint32(std::max<int64>(1, value));
            }
        if (!sObjectMgr->RegisterRuntimePlayerInfo(race, CLASS_DRUID, synthetic, levels))
            return false;
        EventOnlyDruidRaces.insert(race);
    }
    NpcEntry = sConfigMgr->GetOption<uint32>("HabilidadesRandom.NpcEntry", 900100);
    CreatureTemplate const* source = sObjectMgr->GetCreatureTemplate(68);
    if (!source || sObjectMgr->GetCreatureTemplate(NpcEntry))
    {
        LOG_ERROR("server.loading", "HabilidadesRandom: entrada NPC ocupada o falta plantilla base 68. Elegir otra NpcEntry; no se sobrescribirá world.");
        return false;
    }
    CreatureTemplate npc = *source;
    npc.Entry = NpcEntry;
    npc.Name = "Guardián del desafío";
    npc.SubName = "Habilidades aleatorias: nivel 80";
    npc.minlevel = npc.maxlevel = 80;
    npc.faction = 35;
    npc.npcflag = UNIT_NPC_FLAG_GOSSIP;
    npc.unit_flags |= UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC;
    npc.flags_extra = CREATURE_FLAG_EXTRA_CIVILIAN | CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT;
    npc.ScriptID = 0; // AllCreatureScript dispatches without a database ScriptName binding.
    npc.AIName.clear();
    npc.GossipMenuId = 0;
    npc.lootid = npc.pickpocketLootId = npc.SkinLootId = 0;
    npc.mingold = npc.maxgold = 0;
    std::fill(std::begin(npc.DifficultyEntry), std::end(npc.DifficultyEntry), 0);
    std::fill(std::begin(npc.KillCredit), std::end(npc.KillCredit), 0);
    std::fill(std::begin(npc.spells), std::end(npc.spells), 0);
    if (!sObjectMgr->RegisterRuntimeCreatureTemplate(npc))
        return false;
    Prepared = true;
    return true;
#endif
}

void HRRuntime::LoadNpcs()
{
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex);
    if (!Prepared)
        return;
    auto rows = HRDatabaseStore.Query("SELECT id,map_id,x,y,z,o FROM mod_habilidades_random_npcs");
    if (!rows)
        return;
    do
    {
        auto fields = rows->Fetch();
        Position position(fields[2].Get<float>(), fields[3].Get<float>(), fields[4].Get<float>(), fields[5].Get<float>());
        if (!Spawn(fields[0].Get<uint32>(), fields[1].Get<uint32>(), position))
            LOG_ERROR("server.loading", "HabilidadesRandom: no se pudo crear el NPC propio {}.", fields[0].Get<uint32>());
    } while (rows->NextRow());
}

bool HRRuntime::PlaceNpc(Player* player)
{
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex);
    if (!player || !Prepared || !HRDatabaseStore.IsOpen() || !ValidMap(player->GetMapId()) || player->GetTransport())
        return false;
    std::ostringstream sql;
    sql.imbue(std::locale::classic());
    sql.precision(9);
    sql << "INSERT INTO mod_habilidades_random_npcs (map_id,x,y,z,o) VALUES (" << player->GetMapId() << ',' <<
        player->GetPositionX() << ',' << player->GetPositionY() << ',' << player->GetPositionZ() << ',' << player->GetOrientation() << ')';
    uint32 rowId = 0;
    if (!HRDatabaseStore.ExecuteInsert(sql.str(), rowId))
        return false;
    if (Spawn(rowId, player->GetMapId(), player->GetPosition()))
        return true;
    HRDatabaseStore.Execute("DELETE FROM mod_habilidades_random_npcs WHERE id=" + std::to_string(rowId));
    return false;
}

bool HRRuntime::RemoveNpc(Creature* creature)
{
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex);
    if (!creature)
        return false;
    auto found = Spawns.find({creature->GetMapId(), creature->GetGUID()});
    if (found == Spawns.end() || !HRDatabaseStore.Execute("DELETE FROM mod_habilidades_random_npcs WHERE id=" + std::to_string(found->second)))
        return false;
    Spawns.erase(found);
    creature->DespawnOrUnsummon();
    return true;
}

bool HRRuntime::IsNpc(Creature const* creature)
{
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex);
    return Prepared && creature && creature->GetEntry() == NpcEntry;
}

bool HRRuntime::IsEventOnlyDruidRace(uint32 race)
{
    std::lock_guard<std::recursive_mutex> lock(RuntimeMutex);
    return EventOnlyDruidRaces.count(race) != 0;
}
