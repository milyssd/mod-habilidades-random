#include "HabilidadesRandom.h"
#include "HRDatabase.h"
#include "HRCharacterSave.h"
#include "HRSnapshotRules.h"
#include "HRRuntime.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "Random.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>

namespace
{
uint32 Guid(Player const* player) { return player->GetGUID().GetCounter(); }
void Notice(Player* player, char const* message)
{
    ChatHandler(player->GetSession()).SendSysMessage(message);
}
std::string Choices(HRStats const& stats)
{
    return std::to_string(stats.offered[0]) + "," + std::to_string(stats.offered[1]) + "," +
        std::to_string(stats.offered[2]);
}
}

HabilidadesRandomMgr* HabilidadesRandomMgr::instance()
{
    static HabilidadesRandomMgr manager;
    return &manager;
}

HabilidadesRandomMgr::HabilidadesRandomMgr()
{
    // Riding and professions must never be suspended or claimed by the challenge.
    uint32 ids[] = {54197,33388,33391,34090,34091,2259,3101,3464,11611,28596,51304,2366,2368,3570,
        11993,28695,50300,2383,32605,7411,7412,7413,13920,28029,51313,13262,2018,3100,3538,9785,
        29844,51300,45357,45358,45359,45360,45361,45363,55005,4036,4037,4038,12656,30350,49383,
        51306,8163,8167,8168,10768,13697,32678,50305,52158,3908,3909,3910,12180,26790,51309,
        2108,3104,3811,10662,32549,51302,25229,25230,28894,28895,28897,51311,2550,3102,3413,
        18260,33359,51296,818,3273,3274,7924,10846,27028,45544,7620,7732,7731,18248,33095,51294,7738};
    _protected.insert(std::begin(ids), std::end(ids));
}

void HabilidadesRandomMgr::LoadConfig()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _enabled = sConfigMgr->GetOption<bool>("HabilidadesRandom.Enable", true);
    _initialDrafts = std::min(sConfigMgr->GetOption<uint32>("HabilidadesRandom.InitialDrafts", 1), 100u);
    _draftsPerLevel = std::min(sConfigMgr->GetOption<uint32>("HabilidadesRandom.DraftsPerLevel", 1), 10u);
    _initialRerolls = std::min(sConfigMgr->GetOption<uint32>("HabilidadesRandom.InitialRerolls", 5), 10000u);
    _rerollsPerLevel = std::min(sConfigMgr->GetOption<uint32>("HabilidadesRandom.RerollsPerLevel", 2), 100u);
    _initialBans = std::min(sConfigMgr->GetOption<uint32>("HabilidadesRandom.InitialBans", 5), 10000u);
    _poolAmount = std::clamp(sConfigMgr->GetOption<uint32>("HabilidadesRandom.PoolAmount", 45), 3u, 10000u);
    _includeBroken = sConfigMgr->GetOption<bool>("HabilidadesRandom.IncludeRarity5", false);
    _blockTrainerSpells = sConfigMgr->GetOption<bool>("HabilidadesRandom.BlockTrainerSpells", true);
    char const* names[] = {"Common", "Uncommon", "Rare", "Epic", "Legendary", "Broken"};
    float defaults[] = {.50f, .27f, .14f, .06f, .03f, .01f};
    for (uint8 i = 0; i < 6; ++i)
    {
        float value = sConfigMgr->GetOption<float>(std::string("HabilidadesRandom.Distribution.") + names[i], defaults[i]);
        _distribution[i] = std::isfinite(value) ? std::max(value, 0.0f) : defaults[i];
    }
}

void HabilidadesRandomMgr::Initialize()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _ready = false;
    _rarities.clear();
    if (!_storageReady || !_profilesReady)
        return;
    // Index only event identities, not every character's full backup. A failed
    // index closes enrollment/recovery without turning ordinary logins into DB
    // queries or disconnecting characters that never entered the challenge.
    // Retain previous identities on failure so already known participants can
    // still be isolated safely; never discard a loaded live player state here.
    _registryReady = false;
    auto count = HRDatabaseStore.Query("SELECT COUNT(*) FROM mod_habilidades_random_event");
    if (!count)
    {
        LOG_ERROR("server.loading", "HabilidadesRandom: no se pudo cargar el registro del evento; el módulo queda inactivo.");
        return;
    }
    uint64 expected = count->Fetch()[0].Get<uint64>();
    auto registrations = HRDatabaseStore.Query("SELECT player_id,completed,character_pending,original_class FROM mod_habilidades_random_event");
    std::unordered_map<uint32, HREventRegistration> loaded;
    if (registrations)
        do
        {
            Field* fields = registrations->Fetch();
            loaded[fields[0].Get<uint32>()] = {fields[1].Get<bool>(), fields[2].Get<bool>(), fields[3].Get<uint8>()};
        } while (registrations->NextRow());
    if (loaded.size() != expected)
    {
        LOG_ERROR("server.loading", "HabilidadesRandom: registro del evento incompleto; el módulo queda inactivo.");
        return;
    }
    _registrations.swap(loaded);
    _registryReady = true;
    // A failed/empty pool must never cause a destructive spell cleanup.
    QueryResult schema = HRDatabaseStore.Query("SELECT COUNT(*) FROM information_schema.tables WHERE "
        "table_schema=DATABASE() AND table_name IN ('mod_habilidades_random_stats',"
        "'mod_habilidades_random_bans','mod_habilidades_random_drafted_spells',"
        "'mod_habilidades_random_spell_rarity','mod_habilidades_random_event','mod_habilidades_random_snapshot')");
    if (!schema || schema->Fetch()[0].Get<uint32>() != 6)
    {
        LOG_ERROR("server.loading", "HabilidadesRandom: falta el esquema de evento; no se modificarán personajes.");
        return;
    }
    QueryResult result = HRDatabaseStore.Query("SELECT spell_id,rarity FROM mod_habilidades_random_spell_rarity");
    if (!result)
    {
        LOG_ERROR("server.loading", "HabilidadesRandom: pool vacía o inaccesible; las inscripciones quedan cerradas.");
        return;
    }
    do
    {
        Field* fields = result->Fetch();
        uint32 root = Root(fields[0].Get<uint32>());
        uint8 rarity = fields[1].Get<uint8>();
        if (!sSpellMgr->GetSpellInfo(root) || rarity > HR_BROKEN || IsProtected(root))
            continue;
        auto itr = _rarities.find(root);
        if (itr == _rarities.end() || rarity > itr->second)
            _rarities[root] = rarity;
    } while (result->NextRow());
    _ready = !_rarities.empty();
    LOG_INFO("server.loading", "HabilidadesRandom: {} cadenas válidas; evento por inscripción en NPC.", _rarities.size());
}

void HabilidadesRandomMgr::SetAvailability(bool storageReady, bool profilesReady, bool npcReady)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _storageReady = storageReady;
    _profilesReady = profilesReady;
    _npcReady = npcReady;
}

bool HabilidadesRandomMgr::CanRecover() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _storageReady && _profilesReady && _registryReady;
}

uint32 HabilidadesRandomMgr::Root(uint32 id) const
{
    uint32 root = sSpellMgr->GetFirstSpellInChain(id);
    return root ? root : id;
}

bool HabilidadesRandomMgr::IsProtected(uint32 id) const
{
    return _protected.count(id) || _protected.count(Root(id));
}

bool HabilidadesRandomMgr::ShouldSuspend(uint32 id) const
{
    SpellInfo const* spell = sSpellMgr->GetSpellInfo(id);
    return spell && !IsProtected(id) && spell->SpellFamilyName != SPELLFAMILY_GENERIC;
}

bool HabilidadesRandomMgr::IsParticipant(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto itr = _players.find(Guid(player));
    return itr != _players.end() && !itr->second.completed;
}

bool HabilidadesRandomMgr::TalentsLocked(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto itr = _players.find(Guid(player));
    return itr != _players.end() && HRTalentsLocked(true, itr->second.completed && !itr->second.characterPending, itr->second.transitioning);
}

bool HabilidadesRandomMgr::HasHybridPower(Player const* player, uint8 power) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return IsParticipant(player) && (power == POWER_MANA || power == POWER_RAGE || power == POWER_ENERGY);
}

bool HabilidadesRandomMgr::CanLearnSpell(Player const* player, uint32 id) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto itr = _players.find(Guid(player));
    return itr == _players.end() || (itr->second.completed && !itr->second.characterPending) || itr->second.transitioning ||
        !_blockTrainerSpells || !ShouldSuspend(id);
}

bool HabilidadesRandomMgr::TrainersBlocked(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto itr = _players.find(Guid(player));
    return _blockTrainerSpells && itr != _players.end() &&
        (!itr->second.completed || itr->second.characterPending) && !itr->second.transitioning;
}

void HabilidadesRandomMgr::OnLearnSpell(Player* player, uint32 id)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (CanLearnSpell(player, id))
        return;
    auto& state = _players.at(Guid(player));
    // The legacy hook runs after addSpell: immediately remove its server-side effects.
    // learnSpell may send its learned packet AFTER this callback, so correct the client on the next tick too.
    state.rejectedLearned.insert(id);
    player->removeSpell(id, SPEC_MASK_ALL, false);
}

bool HabilidadesRandomMgr::LoadPlayer(Player* player)
{
    uint32 guid = Guid(player);
    auto event = HRDatabaseStore.Query("SELECT completed,original_class,original_spec,original_specs,"
        "spell_count,talent_count,revision,glyph_count,character_pending,completion_level FROM mod_habilidades_random_event WHERE player_id=" + std::to_string(guid));
    if (!event)
        return false;
    HRPlayerState state;
    Field* f = event->Fetch();
    state.completed = f[0].Get<bool>();
    state.characterPending = f[8].Get<bool>();
    state.completionLevel = f[9].Get<uint8>();
    state.runesInitialized = player->getClass() == CLASS_DEATH_KNIGHT;
    state.originalClass = f[1].Get<uint8>();
    _registrations[guid] = {state.completed, state.characterPending, state.originalClass};
    state.originalSpec = f[2].Get<uint8>();
    state.originalSpecs = f[3].Get<uint8>();
    uint32 spells = f[4].Get<uint32>(), talents = f[5].Get<uint32>();
    state.revision = f[6].Get<uint64>();
    uint32 glyphCount = f[7].Get<uint32>();
    uint32 loadedGlyphs = 0;
    auto snapshot = HRDatabaseStore.Query("SELECT spell_id,spec_mask,kind FROM mod_habilidades_random_snapshot "
        "WHERE player_id=" + std::to_string(guid));
    if (snapshot)
        do
        {
            Field* row = snapshot->Fetch();
            HRSavedSpell saved{row[0].Get<uint32>(), row[1].Get<uint8>()};
            uint8 kind = row[2].Get<uint8>();
            if (kind < 2)
                (kind == 0 ? state.spells : state.talents).push_back(saved);
            else if (kind < 8)
            {
                for (uint8 spec = 0; spec < 2; ++spec)
                    if (saved.specMask & (1u << spec))
                        state.glyphs[spec][kind - 2] = saved.id;
                ++loadedGlyphs;
            }
        } while (snapshot->NextRow());
    if (state.spells.size() != spells || state.talents.size() != talents || loadedGlyphs != glyphCount ||
        !sObjectMgr->GetPlayerInfo(player->getRace(), state.originalClass) ||
        state.originalSpecs < 1 || state.originalSpecs > MAX_TALENT_SPECS || state.originalSpec >= state.originalSpecs)
    {
        Notice(player, "[Desafío] Copia de seguridad incompleta. Contacta con un administrador; no se retirarán poderes.");
        return false;
    }
    auto skillHeaderCount = HRDatabaseStore.Query("SELECT COUNT(*) FROM mod_habilidades_random_skill_snapshot_meta WHERE player_id=" +
        std::to_string(guid));
    if (!skillHeaderCount)
        return false;
    state.hasSkillSnapshot = skillHeaderCount->Fetch()[0].Get<uint32>() != 0;
    if (state.hasSkillSnapshot)
    {
        auto skillHeader = HRDatabaseStore.Query("SELECT skill_count FROM mod_habilidades_random_skill_snapshot_meta WHERE player_id=" +
            std::to_string(guid));
        auto skillCount = HRDatabaseStore.Query("SELECT COUNT(*) FROM mod_habilidades_random_skill_snapshot WHERE player_id=" +
            std::to_string(guid));
        if (!skillHeader || !skillCount || skillHeader->Fetch()[0].Get<uint32>() != skillCount->Fetch()[0].Get<uint32>())
            return false;
        uint32 expectedSkills = skillHeader->Fetch()[0].Get<uint32>();
        auto skillRows = HRDatabaseStore.Query("SELECT skill_id,skill_step,skill_value,skill_max FROM mod_habilidades_random_skill_snapshot WHERE player_id=" +
            std::to_string(guid));
        if (skillRows)
            do
            {
                Field* row = skillRows->Fetch();
                HRSavedSkill saved{row[0].Get<uint16>(), row[1].Get<uint16>(), row[2].Get<uint16>(), row[3].Get<uint16>()};
                if (!saved.value || saved.value > saved.maximum ||
                    !GetSkillRaceClassInfo(saved.id, player->getRace(), state.originalClass))
                    return false;
                state.skills.push_back(saved);
            } while (skillRows->NextRow());
        if (state.skills.size() != expectedSkills)
            return false;
    }
    else if (!state.completed || state.characterPending)
    {
        // An older event has no historical skill values. Do not fabricate a backup
        // from its already-converted character, or prevent its class restoration.
        LOG_WARN("server.loading", "HabilidadesRandom: personaje {} con copia anterior sin destrezas; no se pueden recuperar valores históricos desconocidos.", guid);
        Notice(player, "[Desafío] Copia antigua sin destrezas originales. Se recuperarán clase y poderes; los puntos de destrezas ya perdidos necesitan una copia anterior del servidor.");
    }
    auto stats = HRDatabaseStore.Query("SELECT successful_drafts,total_expected_drafts,rerolls,bans,"
        "offered_spell_1,offered_spell_2,offered_spell_3,last_level FROM mod_habilidades_random_stats WHERE player_id=" +
        std::to_string(guid));
    if (!stats)
        return false;
    f = stats->Fetch();
    state.stats.playerId = guid;
    state.stats.successful = f[0].Get<uint32>();
    state.stats.expected = f[1].Get<uint32>();
    state.stats.rerolls = f[2].Get<uint32>();
    state.stats.bans = f[3].Get<uint32>();
    for (uint8 i = 0; i < 3; ++i)
        state.stats.offered[i] = f[4 + i].Get<uint32>();
    state.stats.lastLevel = f[7].Get<uint8>();
    // COUNT distinguishes an empty set from a failed SELECT.
    for (bool bans : {false, true})
    {
        std::string table = bans ? "mod_habilidades_random_bans" : "mod_habilidades_random_drafted_spells";
        std::string column = bans ? "player_id" : "player_guid";
        auto count = HRDatabaseStore.Query("SELECT COUNT(*) FROM " + table + " WHERE " + column + "=" + std::to_string(guid));
        if (!count)
            return false;
        uint32 expected = count->Fetch()[0].Get<uint32>();
        auto rows = HRDatabaseStore.Query("SELECT spell_id FROM " + table + " WHERE " + column + "=" + std::to_string(guid));
        auto& values = bans ? state.bans : state.drafted;
        if (rows)
            do { values.insert(rows->Fetch()[0].Get<uint32>()); } while (rows->NextRow());
        if (values.size() != expected)
            return false;
    }
    _players[guid] = std::move(state);
    return true;
}

bool HabilidadesRandomMgr::SaveCharacter(Player* player)
{
    if (player->IsBeingTeleportedFar())
        return false; // SaveToDB otherwise defers the character row itself.
    // Use only the core's own character-save APIs and prepared statements.
    // No module schema, custom query or cross-schema SQL goes to CharacterDatabase.
    std::vector<HRSavedSpell> spells, talents;
    std::vector<HRSavedSkill> skills;
    for (auto const& pair : player->GetSpellMap())
        if (pair.second->State != PLAYERSPELL_REMOVED && pair.second->State != PLAYERSPELL_TEMPORARY)
            spells.push_back({pair.first, pair.second->specMask});
    for (auto const& pair : player->GetTalentMap())
        if (pair.second->State != PLAYERSPELL_REMOVED && pair.second->State != PLAYERSPELL_TEMPORARY)
            talents.push_back({pair.first, pair.second->specMask});
    for (auto const& pair : player->GetSkillStatusMap())
        if (pair.second.uState != SKILL_DELETED)
        {
            uint16 id = uint16(pair.first);
            skills.push_back({id, player->GetSkillStep(id), player->GetPureSkillValue(id), player->GetPureMaxSkillValue(id)});
        }
    auto transaction = std::make_shared<HRCharacterSaveTransaction>();
    player->SetNeedToSaveGlyphs(true);
    player->SaveToDB(transaction, false, false);
    // Reconcile membership after a retry, even if an earlier failed core save cleared its dirty flags.
    for (auto index : {CHAR_DEL_CHAR_SPELL, CHAR_DEL_CHAR_TALENT, CHAR_DEL_CHAR_SKILLS})
    {
        auto statement = CharacterDatabase.GetPreparedStatement(index);
        statement->SetData(0, Guid(player));
        transaction->Append(statement);
    }
    for (auto const& spell : spells)
    {
        auto statement = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_SPELL);
        statement->SetData(0, Guid(player));
        statement->SetData(1, spell.id);
        statement->SetData(2, spell.specMask);
        transaction->Append(statement);
    }
    for (auto const& talent : talents)
    {
        auto statement = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_TALENT);
        statement->SetData(0, Guid(player));
        statement->SetData(1, talent.id);
        statement->SetData(2, talent.specMask);
        transaction->Append(statement);
    }
    // _SaveSkills also consumes its dirty flags before the transaction commits.
    // Rebuild the complete live projection so retrying a failed save preserves
    // restored weapon/class skills and profession progress equally.
    for (auto const& skill : skills)
    {
        auto statement = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_SKILLS);
        statement->SetData(0, Guid(player));
        statement->SetData(1, skill.id);
        statement->SetData(2, skill.value);
        statement->SetData(3, skill.maximum);
        transaction->Append(statement);
    }
    return HRCommitCharacterSave(transaction);
}

bool HabilidadesRandomMgr::Save(Player* player, HRPlayerState& state, bool savePlayer)
{
    if (_recoveryRequired.count(Guid(player)))
        return false;
    if (!CanRecover())
    {
        RequireRecovery(player);
        return false;
    }
    auto transaction = std::make_shared<HRTransaction>();
    std::string guid = std::to_string(Guid(player));
    HRStats const& s = state.stats;
    std::ostringstream sql;
    sql << "INSERT INTO mod_habilidades_random_stats VALUES (" << guid << ',' << s.successful << ',' << s.expected
        << ',' << s.rerolls << ',' << s.bans << ',' << s.offered[0] << ',' << s.offered[1] << ',' << s.offered[2]
        << ',' << uint32(s.lastLevel) << ") ON DUPLICATE KEY UPDATE successful_drafts=VALUES(successful_drafts),"
        "total_expected_drafts=VALUES(total_expected_drafts),rerolls=VALUES(rerolls),bans=VALUES(bans),"
        "offered_spell_1=VALUES(offered_spell_1),offered_spell_2=VALUES(offered_spell_2),"
        "offered_spell_3=VALUES(offered_spell_3),last_level=VALUES(last_level)";
    transaction->Append(sql.str());
    transaction->Append("DELETE FROM mod_habilidades_random_drafted_spells WHERE player_guid=" + guid);
    for (uint32 id : state.drafted)
        transaction->Append("INSERT INTO mod_habilidades_random_drafted_spells VALUES (" + guid + ',' + std::to_string(id) + ')');
    transaction->Append("DELETE FROM mod_habilidades_random_bans WHERE player_id=" + guid);
    for (uint32 id : state.bans)
        transaction->Append("INSERT INTO mod_habilidades_random_bans VALUES (" + guid + ',' + std::to_string(id) + ')');
    uint64 revision = state.revision + 1;
    bool pending = state.characterPending || savePlayer;
    transaction->Append("UPDATE mod_habilidades_random_event SET completed=" + std::to_string(state.completed) +
        ",completion_level=" + std::to_string(state.completionLevel) + ",character_pending=" + std::to_string(pending) + ",revision=" + std::to_string(revision) + " WHERE player_id=" + guid);
    // The private DB is the durable source of truth. It commits BEFORE the core saves its character projection.
    if (!HRDatabaseStore.Commit(transaction))
    {
        Notice(player, "[Desafío] No se pudo guardar el evento. Se mantiene la copia original.");
        RequireRecovery(player);
        return false;
    }
    state.revision = revision;
    state.characterPending = pending;
    _registrations[Guid(player)] = {state.completed, state.characterPending, state.originalClass};
    if (savePlayer && SaveCharacter(player))
    {
        if (HRDatabaseStore.Execute("UPDATE mod_habilidades_random_event SET character_pending=0 WHERE player_id=" +
            guid + " AND revision=" + std::to_string(revision)))
            state.characterPending = false;
    }
    else if (savePlayer)
    {
        // SaveToDB consumes dirty flags for inventory and other core state too.
        // Reload from durable state before replaying the event projection;
        // continuing in this session could silently omit those changes on retry.
        RequireRecovery(player);
    }
    if (savePlayer && state.characterPending)
        Notice(player, _recoveryRequired.count(Guid(player)) ?
            "[Desafío] Progreso protegido en la base del evento. Vuelve a conectar para completar la recuperación." :
            "[Desafío] Progreso protegido en la base del evento. El guardado del personaje se reintentará automáticamente.");
    _registrations[Guid(player)] = {state.completed, state.characterPending, state.originalClass};
    return true;
}

void HabilidadesRandomMgr::RequireRecovery(Player* player)
{
#ifdef ACORE_MODULE_SAVE_GUARD
    // Core SaveToDB already consumed dirty flags before its failed commit.
    // Suppress logout/autosave as well: another save could persist changed gold
    // while silently omitting the item whose dirty flag was consumed.
    player->SetModuleSaveSuppressed(true);
#endif
    // A lost COMMIT response does not prove rollback. Stop this session from
    // overwriting a possibly newer durable revision with its stale in-memory
    // copy; the next login reads whichever transaction actually committed.
    _recoveryRequired.insert(Guid(player));
    Close(player);
    player->GetSession()->KickPlayer(std::string("HabilidadesRandom: recuperación pendiente; revisa la base del evento y vuelve a conectar"));
}

void HabilidadesRandomMgr::ChangeClass(Player* player, uint8 classId)
{
    player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
    player->RemoveAurasByType(SPELL_AURA_MOUNTED);
    // Only the class byte changes: race, gender and display IDs are untouched.
    player->SetByteValue(UNIT_FIELD_BYTES_0, 1, classId);
    if (CharacterCacheEntry const* cached = sCharacterCache->GetCharacterCacheByGuid(player->GetGUID()))
    {
        // Re-adding updates the class but resets guild/arena fields. Preserve them,
        // and keep the existing mail count and group entry in the core's cache.
        CharacterCacheEntry saved = *cached;
        sCharacterCache->AddCharacterCacheEntry(saved.Guid, saved.AccountId, saved.Name,
            saved.Sex, saved.Race, classId, player->GetLevel());
        sCharacterCache->UpdateCharacterGuildId(saved.Guid, saved.GuildId);
        for (uint8 slot = 0; slot < saved.ArenaTeamId.size(); ++slot)
            sCharacterCache->UpdateCharacterArenaTeamId(saved.Guid, slot, saved.ArenaTeamId[slot]);
    }
    player->setPowerType(classId == CLASS_WARRIOR ? POWER_RAGE :
        classId == CLASS_ROGUE ? POWER_ENERGY : classId == CLASS_DEATH_KNIGHT ? POWER_RUNIC_POWER : POWER_MANA);
    auto& state = _players.at(Guid(player));
    if (classId == CLASS_DEATH_KNIGHT && !state.runesInitialized)
    {
        player->InitRunes();
        state.runesInitialized = true;
    }
    player->InitStatsForLevel(true);
    player->UpdateAllStats();
    player->SetFullHealth();
    player->SetPower(POWER_MANA, player->GetMaxPower(POWER_MANA));
}

bool HabilidadesRandomMgr::Join(Player* player)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_recoveryRequired.count(Guid(player)))
        return false;
    if (!IsEnabled() || player->GetLevel() >= 80 || !player->IsAlive() || player->IsInCombat() ||
        player->IsBeingTeleported() || player->IsMounted())
    {
        Notice(player, "[Desafío] Debes estar vivo, fuera de combate, sin montura y por debajo del nivel 80.");
        return false;
    }
    if (!sObjectMgr->GetPlayerInfo(player->getRace(), CLASS_DRUID))
    {
        Notice(player, "[Desafío] No está preparado el perfil druida de tu raza; revisar el parche runtime y el arranque del módulo.");
        return false;
    }
    uint32 available = 0;
    for (auto const& spell : _rarities)
        if ((_includeBroken || spell.second != HR_BROKEN) && _distribution[spell.second] > 0 && Eligible(spell.first, player))
            if (++available == 3)
                break;
    if (available < 3)
    {
        Notice(player, "[Desafío] No hay tres poderes disponibles para tu nivel con esta configuración. No se ha modificado tu personaje.");
        return false;
    }
    auto existing = HRDatabaseStore.Query("SELECT COUNT(*) FROM mod_habilidades_random_event WHERE player_id=" + std::to_string(Guid(player)));
    auto legacy = HRDatabaseStore.Query("SELECT COUNT(*) FROM mod_habilidades_random_drafted_spells WHERE player_guid=" + std::to_string(Guid(player)));
    if (!existing || !legacy || existing->Fetch()[0].Get<uint32>() || legacy->Fetch()[0].Get<uint32>())
    {
        Notice(player, "[Desafío] Ya existe un evento o registro antiguo para este personaje. No se sobrescribirá su copia.");
        return false;
    }
    HRPlayerState state;
    state.originalClass = player->getClass();
    state.originalSpec = player->GetActiveSpec();
    state.originalSpecs = player->GetSpecsCount();
    state.stats.playerId = Guid(player);
    state.stats.expected = _initialDrafts;
    state.stats.rerolls = _initialRerolls;
    state.stats.bans = _initialBans;
    state.stats.lastLevel = player->GetLevel();
    for (auto const& pair : player->GetSpellMap())
        if (pair.second->State != PLAYERSPELL_REMOVED && pair.second->State != PLAYERSPELL_TEMPORARY)
            state.spells.push_back({pair.first, pair.second->specMask});
    for (auto const& pair : player->GetTalentMap())
        if (pair.second->State != PLAYERSPELL_REMOVED && pair.second->State != PLAYERSPELL_TEMPORARY)
            state.talents.push_back({pair.first, pair.second->specMask});
    // _LoadSkills discards class-incompatible skills before the login hook runs.
    // Back up only those at risk; professions and shared skills retain live progress.
    for (auto const& pair : player->GetSkillStatusMap())
        if (pair.second.uState != SKILL_DELETED &&
            GetSkillRaceClassInfo(pair.first, player->getRace(), state.originalClass) &&
            !GetSkillRaceClassInfo(pair.first, player->getRace(), CLASS_DRUID))
        {
            uint16 id = uint16(pair.first);
            state.skills.push_back({id, player->GetSkillStep(id), player->GetPureSkillValue(id), player->GetPureMaxSkillValue(id)});
        }
    uint32 glyphCount = 0;
    for (uint8 spec = 0; spec < state.originalSpecs; ++spec)
    {
        player->SetActiveSpec(spec);
        for (uint8 slot = 0; slot < 6; ++slot)
            state.glyphs[spec][slot] = player->GetGlyph(slot);
    }
    player->SetActiveSpec(state.originalSpec);
    for (uint8 slot = 0; slot < 6; ++slot)
        glyphCount += (state.glyphs[0][slot] != 0) + (state.glyphs[1][slot] != 0 && state.glyphs[1][slot] != state.glyphs[0][slot]);
    auto transaction = std::make_shared<HRTransaction>();
    std::string guid = std::to_string(Guid(player));
    transaction->Append("INSERT INTO mod_habilidades_random_event (player_id,original_class,original_spec,original_specs,"
        "spell_count,talent_count,glyph_count) VALUES (" + guid + ',' + std::to_string(state.originalClass) + ',' +
        std::to_string(state.originalSpec) + ',' + std::to_string(state.originalSpecs) + ',' +
        std::to_string(state.spells.size()) + ',' + std::to_string(state.talents.size()) + ',' + std::to_string(glyphCount) + ')');
    // Even an empty new snapshot has a header. Its absence means a legacy backup,
    // never a license to guess values from a character that already joined.
    transaction->Append("INSERT INTO mod_habilidades_random_skill_snapshot_meta VALUES (" + guid + ',' +
        std::to_string(state.skills.size()) + ')');
    for (HRSavedSkill const& skill : state.skills)
        transaction->Append("INSERT INTO mod_habilidades_random_skill_snapshot VALUES (" + guid + ',' +
            std::to_string(skill.id) + ',' + std::to_string(skill.step) + ',' + std::to_string(skill.value) + ',' +
            std::to_string(skill.maximum) + ')');
    for (uint8 kind = 0; kind < 2; ++kind)
        for (HRSavedSpell const& spell : kind == 0 ? state.spells : state.talents)
            transaction->Append("INSERT INTO mod_habilidades_random_snapshot VALUES (" + guid + ',' +
                std::to_string(spell.id) + ',' + std::to_string(spell.specMask) + ',' + std::to_string(kind) + ')');
    for (uint8 slot = 0; slot < 6; ++slot)
        for (uint8 spec = 0; spec < state.originalSpecs; ++spec)
        {
            uint32 id = state.glyphs[spec][slot];
            if (id && !(spec == 1 && id == state.glyphs[0][slot]))
            {
                uint8 mask = (state.glyphs[0][slot] == id ? 1 : 0) | (state.glyphs[1][slot] == id ? 2 : 0);
                transaction->Append("INSERT INTO mod_habilidades_random_snapshot VALUES (" + guid + ',' +
                    std::to_string(id) + ',' + std::to_string(mask) + ',' + std::to_string(slot + 2) + ')');
            }
        }
    // Persist stats in the same transaction as the immutable snapshot.
    transaction->Append("INSERT INTO mod_habilidades_random_stats (player_id,total_expected_drafts,rerolls,bans,last_level) VALUES (" +
        guid + ',' + std::to_string(state.stats.expected) + ',' + std::to_string(state.stats.rerolls) + ',' +
        std::to_string(state.stats.bans) + ',' + std::to_string(state.stats.lastLevel) + ") ON DUPLICATE KEY UPDATE "
        "successful_drafts=0,total_expected_drafts=VALUES(total_expected_drafts),rerolls=VALUES(rerolls),bans=VALUES(bans),"
        "offered_spell_1=0,offered_spell_2=0,offered_spell_3=0,last_level=VALUES(last_level)");
    // COMMIT can succeed even if its acknowledgement is lost. Keep the identity
    // before attempting it so a later login checks for that possible backup.
    _registrations[Guid(player)] = {false, true, state.originalClass};
    if (!HRDatabaseStore.Commit(transaction))
    {
        Notice(player, "[Desafío] No se pudo guardar la copia. Tu personaje no ha sido modificado.");
        RequireRecovery(player);
        return false;
    }
    // Read back every backup before removing a single original spell.
    if (!LoadPlayer(player))
    {
        RequireRecovery(player);
        return false;
    }
    Suspend(player);
    auto& current = _players.at(Guid(player));
    if (!Save(player, current, true))
        return false;
    if (_recoveryRequired.count(Guid(player)))
        return false;
    Notice(player, "[Desafío] Inscrito: druida temporal sin cambiar tu raza. Talentos bloqueados hasta el nivel 80.");
    Check(player);
    return true;
}

void HabilidadesRandomMgr::SuspendTalents(Player* player)
{
    auto& state = _players.at(Guid(player));
    bool transitioning = state.transitioning;
    state.transitioning = true;
    uint8 active = player->GetActiveSpec();
    // Reset both specs, not just the currently selected one. Backup stays immutable.
    for (uint8 spec = 0; spec < player->GetSpecsCount(); ++spec)
    {
        player->ActivateSpec(spec);
        for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
            if (GlyphPropertiesEntry const* glyph = sGlyphPropertiesStore.LookupEntry(player->GetGlyph(slot)))
                player->RemoveAurasDueToSpell(glyph->SpellId);
        player->resetTalents(true);
    }
    player->ActivateSpec(active);
    // ActivateSpec reapplies glyph auras for the active spec.
    for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
        if (GlyphPropertiesEntry const* glyph = sGlyphPropertiesStore.LookupEntry(player->GetGlyph(slot)))
            player->RemoveAurasDueToSpell(glyph->SpellId);
    state.transitioning = transitioning;
    player->SetFreeTalentPoints(0);
    player->SendTalentsInfoData(false);
}

void HabilidadesRandomMgr::Suspend(Player* player)
{
    auto& state = _players.at(Guid(player));
    state.transitioning = true;
    SuspendTalents(player);
    if (Pet* pet = player->GetPet())
        player->RemovePet(pet, PET_SAVE_NOT_IN_SLOT, true);
    std::vector<uint32> remove;
    for (auto const& pair : player->GetSpellMap())
        if (pair.second->State != PLAYERSPELL_REMOVED && ShouldSuspend(pair.first) && !state.drafted.count(pair.first))
            remove.push_back(pair.first);
    for (uint32 id : remove)
        player->removeSpell(id, SPEC_MASK_ALL, false);
    if (player->getClass() != CLASS_DRUID)
        ChangeClass(player, CLASS_DRUID);
    // Resource capacities, not an infinite refill on every server tick.
    player->SetMaxPower(POWER_RAGE, 1000);
    player->SetMaxPower(POWER_ENERGY, 100);
    state.transitioning = false;
    UpgradeDrafted(player);
}

bool HabilidadesRandomMgr::Restore(Player* player)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_recoveryRequired.count(Guid(player)))
        return false;
    auto itr = _players.find(Guid(player));
    if (itr == _players.end() || itr->second.completed)
        return false;
    if (!CanRecover())
    {
        RequireRecovery(player);
        return false;
    }
    if (!player->IsAlive() || player->IsInCombat() || player->IsBeingTeleported())
        return false; // Retried automatically after combat/resurrection/teleport.
    auto& state = itr->second;
    auto restoreSkills = state.skills;
    // Spell/talent cleanup can remove a dependent skill. Preserve its current
    // progress before cleanup, without changing the immutable enrollment backup.
    for (HRSavedSkill& skill : restoreSkills)
    {
        skill.step = std::max(skill.step, player->GetSkillStep(skill.id));
        skill.value = std::max(skill.value, player->GetPureSkillValue(skill.id));
        skill.maximum = std::max(skill.maximum, player->GetPureMaxSkillValue(skill.id));
    }
    state.transitioning = true;
    SuspendTalents(player);
    // Also remove automatically loaded druid class spells that are not draft-owned.
    std::vector<uint32> remove;
    for (auto const& pair : player->GetSpellMap())
        if (pair.second->State != PLAYERSPELL_REMOVED &&
            (ShouldSuspend(pair.first) || state.drafted.count(pair.first)))
            remove.push_back(pair.first);
    for (uint32 id : remove)
        player->removeSpell(id, SPEC_MASK_ALL, false);
    // If completion committed before the core character save, recover the earned level too.
    if (state.characterPending && state.completionLevel >= 80 && player->GetLevel() < 80)
    {
        player->SetLevel(80);
        player->SetUInt32Value(PLAYER_XP, 0);
    }
    ChangeClass(player, state.originalClass);
    player->SetSpecsCount(state.originalSpecs);
    player->ActivateSpec(state.originalSpec);
    bool ok = true;
    for (HRSavedSpell const& spell : state.talents)
    {
        // This fork's HasTalent(id, spec) checks the active spec even when an
        // explicit spec is supplied. Verify the persisted mask directly instead.
        bool restored = HRRestoreTalentSpecs(spell.specMask, state.originalSpecs, [&]
        {
            auto const& talents = player->GetTalentMap();
            auto found = talents.find(spell.id);
            return found != talents.end() && found->second->State != PLAYERSPELL_REMOVED ? found->second->specMask : uint8(0);
        }, [&](uint8 mask) { player->addTalent(spell.id, mask, 0); });
        ok = ok && restored;
    }
    for (HRSavedSpell const& spell : state.spells)
    {
        player->addSpell(spell.id, spell.specMask, true);
        auto found = player->GetSpellMap().find(spell.id);
        ok = ok && found != player->GetSpellMap().end() && found->second->State != PLAYERSPELL_REMOVED &&
            (spell.specMask & ~found->second->specMask) == 0;
    }
    for (HRSavedSkill const& skill : restoreSkills)
    {
        // Apply and verify skills after all spell-driven skill initialization.
        // Restore a minimum, preserving any higher current value/rank. Shared
        // professions and generic skills have no snapshot and keep live progress.
        uint16 step = std::max(skill.step, player->GetSkillStep(skill.id));
        uint16 value = std::max(skill.value, player->GetPureSkillValue(skill.id));
        uint16 maximum = std::max(skill.maximum, player->GetPureMaxSkillValue(skill.id));
        if (SkillRaceClassInfoEntry const* entry = GetSkillRaceClassInfo(skill.id, player->getRace(), state.originalClass))
            if (GetSkillRangeType(entry) == SKILL_RANGE_LEVEL && maximum != 1)
                maximum = std::max(maximum, player->GetMaxSkillValueForLevel());
        player->SetSkill(skill.id, step, value, maximum);
        ok = ok && player->GetSkillStep(skill.id) >= step && player->GetPureSkillValue(skill.id) >= value &&
            player->GetPureMaxSkillValue(skill.id) >= maximum;
    }
    if (!ok)
    {
        Notice(player, "[Desafío] Restauración incompleta: se conserva la copia para reintentarlo al conectar.");
        state.transitioning = false;
        return false;
    }
    uint8 currentSpec = player->GetActiveSpec();
    for (uint8 spec = 0; spec < state.originalSpecs; ++spec)
    {
        player->SetActiveSpec(spec);
        for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
            player->SetGlyph(slot, state.glyphs[spec][slot], true);
    }
    player->SetActiveSpec(currentSpec);
    // Restore original glyph effects, which were suspended without changing the glyph records.
    for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
        if (GlyphPropertiesEntry const* glyph = sGlyphPropertiesStore.LookupEntry(player->GetGlyph(slot)))
            player->CastSpell(player, glyph->SpellId, true);
    state.completed = true;
    state.completionLevel = player->GetLevel() >= 80 ? 80 : 0;
    player->InitTalentForLevel();
    player->SendTalentsInfoData(false);
    player->UpdateAllStats();
    state.stats.offered = {};
    if (!Save(player, state, true))
    {
        state.completed = false;
        state.transitioning = false;
        return false;
    }
    state.transitioning = false;
    // addSpell reconstructs server state without learned packets on the legacy
    // core. Refresh the spellbook only after the complete restoration succeeds.
    player->SendInitialSpells();
    Close(player);
    if (state.characterPending)
        return true;
    Notice(player, player->GetLevel() >= 80 ?
        "[Desafío] Completado al nivel 80: clase, poderes y talentos originales restaurados. Ya puedes usar entrenadores." :
        "[Desafío] Evento cerrado: clase, poderes y talentos originales restaurados.");
    return true;
}

void HabilidadesRandomMgr::OnLogin(Player* player)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    uint32 guid = Guid(player);
    _recoveryRequired.erase(guid);
    auto registration = _registrations.find(guid);
    bool eventOnlyDruid = player->getClass() == CLASS_DRUID && HRRuntime::IsEventOnlyDruidRace(player->getRace());
    if (registration != _registrations.end() && registration->second.completed &&
        !registration->second.characterPending && player->getClass() == registration->second.originalClass)
        return;
    if (!CanRecover())
    {
        // With cold-start Enable=0 no DB is opened and identities are unknown.
        // Only a confirmed event identity or a synthetic race/druid combination
        // can be isolated; an outage must never reject unrelated players.
        if (registration != _registrations.end() || eventOnlyDruid)
            RequireRecovery(player);
        return;
    }
    if (registration == _registrations.end())
    {
        if (eventOnlyDruid)
            RequireRecovery(player);
        return;
    }
    // Only confirmed identities (including uncertain enrollment commits) need
    // a backup query. A failed query cannot turn one into a nonparticipant.
    auto count = HRDatabaseStore.Query("SELECT COUNT(*) FROM mod_habilidades_random_event WHERE player_id=" +
        std::to_string(guid));
    if (!count)
    {
        RequireRecovery(player);
        return;
    }
    if (!count->Fetch()[0].Get<uint32>())
    {
        _registrations.erase(guid);
        _players.erase(guid);
        if (eventOnlyDruid)
            RequireRecovery(player);
        return;
    }
    // Recovery remains available even if new enrollment is disabled or the pool fails.
    if (!LoadPlayer(player))
    {
        RequireRecovery(player);
        return;
    }
    auto& state = _players.at(Guid(player));
    if (state.completed)
    {
        // Replay a pending character save after either DB or process failed between the two commits.
        if (!state.characterPending && player->getClass() == state.originalClass)
            return;
        state.completed = false;
        Restore(player);
        return;
    }
    if (HRShouldRestore(true, state.completed, player->GetLevel()) || !IsEnabled())
    {
        Restore(player);
        return;
    }
    Suspend(player);
    // Recover levels gained between the last module commit and a disconnect.
    if (player->GetLevel() > state.stats.lastLevel)
        OnLevelChanged(player, state.stats.lastLevel);
    else
    {
        Save(player, state, true);
        Check(player);
    }
}

void HabilidadesRandomMgr::Update(Player* player, uint32 diff)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_recoveryRequired.count(Guid(player)))
        return;
    auto itr = _players.find(Guid(player));
    if (itr == _players.end() || itr->second.transitioning)
        return;
    if (!CanRecover())
    {
        if (!itr->second.completed || itr->second.characterPending)
            RequireRecovery(player);
        return;
    }
    if (!itr->second.rejectedLearned.empty())
    {
        auto rejected = std::move(itr->second.rejectedLearned);
        itr->second.rejectedLearned.clear();
        bool removed = false;
        for (uint32 id : rejected)
            if (!itr->second.drafted.count(id) && !CanLearnSpell(player, id))
            {
                if (player->HasSpell(id))
                    player->removeSpell(id, SPEC_MASK_ALL, false);
                player->SendLearnPacket(id, false);
                removed = true;
            }
        if (removed)
            Notice(player, "[Desafío] Hechizo normal bloqueado. Usa las elecciones del evento hasta completar el modo.");
    }
    auto& timer = itr->second.updateTimer;
    if (diff < timer)
    {
        timer -= diff;
        return;
    }
    timer = 1000;
    if (itr->second.completed)
    {
        if (itr->second.characterPending)
            Save(player, itr->second, true);
        return;
    }
    if (HRShouldRestore(true, itr->second.completed, player->GetLevel()) || !IsEnabled())
        Restore(player);
    else if (itr->second.characterPending)
        Save(player, itr->second, true);
}

void HabilidadesRandomMgr::OnLogout(Player* player)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _players.erase(Guid(player));
    _recoveryRequired.erase(Guid(player));
}

void HabilidadesRandomMgr::OnSpecChanged(Player* player)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_recoveryRequired.count(Guid(player)))
        return;
    auto itr = _players.find(Guid(player));
    if (itr == _players.end() || itr->second.completed || itr->second.transitioning)
        return;
    SuspendTalents(player);
    UpgradeDrafted(player);
}

void HabilidadesRandomMgr::OnLevelChanged(Player* player, uint8 /*oldLevel*/)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_recoveryRequired.count(Guid(player)))
        return;
    auto itr = _players.find(Guid(player));
    if (itr == _players.end() || itr->second.completed || itr->second.transitioning)
        return;
    if (HRShouldRestore(true, itr->second.completed, player->GetLevel()) || !IsEnabled())
    {
        Restore(player);
        return;
    }
    auto next = itr->second;
    // Keep a high-water mark: lowering/re-raising level cannot farm drafts or erase the backup.
    next.stats.AwardLevels(player->GetLevel(), _draftsPerLevel, _rerollsPerLevel);
    if (!Save(player, next))
        return;
    itr->second = std::move(next);
    UpgradeDrafted(player);
    Check(player);
}

bool HabilidadesRandomMgr::Eligible(uint32 id, Player const* player) const
{
    SpellInfo const* spell = sSpellMgr->GetSpellInfo(id);
    if (!spell || spell->SpellLevel > player->GetLevel() || IsProtected(id) ||
        !SpellMgr::CheckSpellValid(spell, id, false) || spell->HasEffect(SPELL_EFFECT_LEARN_SPELL) ||
        spell->HasEffect(SPELL_EFFECT_TAMECREATURE) || spell->HasEffect(SPELL_EFFECT_CREATE_TAMED_PET) ||
        spell->HasEffect(SPELL_EFFECT_ACTIVATE_RUNE))
        return false;
    for (auto const& effect : spell->Effects)
        if (effect.Effect == SPELL_EFFECT_SUMMON_PET && !effect.MiscValue)
        {
            PetStable const* stable = player->GetPetStable();
            if (!stable || !Pet::GetLoadPetInfo(*stable, 0, 0, false).first)
                return false;
        }
    // The core restricts taming to hunters. A druid has mana, rage and energy;
    // it does not acquire a DK's rune system or pet focus.
    return spell->PowerType == POWER_MANA || spell->PowerType == POWER_RAGE ||
        spell->PowerType == POWER_ENERGY || spell->PowerType == POWER_HEALTH;
}

std::vector<uint32> HabilidadesRandomMgr::RandomSpells(Player* player, uint32 count,
    std::unordered_set<uint32> const& exclude) const
{
    auto const& state = _players.at(Guid(player));
    std::vector<uint32> categories[6];
    for (auto const& pair : _rarities)
    {
        uint32 id = pair.first;
        if ((!_includeBroken && pair.second == HR_BROKEN) || !Eligible(id, player) ||
            state.bans.count(id) || state.drafted.count(id) || exclude.count(id) || player->HasSpell(id))
            continue;
        categories[pair.second].push_back(id);
    }
    std::vector<uint32> pool;
    auto shuffle = [](std::vector<uint32>& values)
    {
        for (size_t i = values.size(); i > 1; --i)
            std::swap(values[i - 1], values[urand(0, uint32(i - 1))]);
    };
    std::array<std::size_t, 6> counts{};
    for (uint8 rarity = 0; rarity < 6; ++rarity)
    {
        shuffle(categories[rarity]);
        counts[rarity] = categories[rarity].size();
    }
    while (pool.size() < _poolAmount)
    {
        std::size_t rarity = HRChooseWeightedCategory(counts, _distribution, rand_norm());
        if (rarity == counts.size())
            break;
        pool.push_back(categories[rarity].back());
        categories[rarity].pop_back();
        --counts[rarity];
    }
    shuffle(pool);
    if (pool.size() > count)
        pool.resize(count);
    return pool;
}

bool HabilidadesRandomMgr::ValidOffer(Player* player, HRPlayerState const& state) const
{
    std::unordered_set<uint32> seen;
    for (uint32 id : state.stats.offered)
        if (!id || !_rarities.count(id) || (!_includeBroken && _rarities.at(id) == HR_BROKEN) ||
            _distribution[_rarities.at(id)] <= 0.0f || !Eligible(id, player) || state.bans.count(id) ||
            state.drafted.count(id) || player->HasSpell(id) || !seen.insert(id).second)
            return false;
    return true;
}

bool HabilidadesRandomMgr::Generate(Player* player)
{
    auto next = _players.at(Guid(player));
    if (!next.stats.Remaining())
        return false;
    auto choices = RandomSpells(player, 3, {});
    if (choices.size() < 3)
    {
        Notice(player, "[Desafío] No hay tres poderes elegibles. Tus elecciones y saldos se conservan para niveles posteriores.");
        return false;
    }
    std::copy(choices.begin(), choices.end(), next.stats.offered.begin());
    if (!Save(player, next))
        return false;
    _players.at(Guid(player)) = std::move(next);
    return true;
}

void HabilidadesRandomMgr::Send(Player* player, std::string const& prefix, std::string const& message) const
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, prefix + '\t' + message);
    player->GetSession()->SendPacket(&packet);
}

void HabilidadesRandomMgr::Close(Player* player) const
{
    Send(player, "SpellChoiceStatus", "inactive");
    Send(player, "SpellChoiceClose", "");
    Send(player, "SpellChoiceDrafts", "0");
}

void HabilidadesRandomMgr::Check(Player* player)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_recoveryRequired.count(Guid(player)))
    {
        Close(player);
        return;
    }
    if (!IsParticipant(player) || player->GetLevel() >= 80 || !IsEnabled())
    {
        if (IsParticipant(player))
            Restore(player);
        Close(player);
        return;
    }
    auto& state = _players.at(Guid(player));
    Send(player, "SpellChoiceStatus", "active");
    if (state.stats.Remaining() && !ValidOffer(player, state))
        Generate(player);
    if (_recoveryRequired.count(Guid(player)))
        return;
    Send(player, "SpellChoiceDrafts", std::to_string(state.stats.Remaining()));
    Send(player, "SpellChoiceRerolls", std::to_string(state.stats.rerolls));
    Send(player, "SpellChoiceBansLeft", std::to_string(state.stats.bans));
    std::string bans;
    Send(player, "SpellChoiceBansReset", "");
    for (uint32 id : state.bans)
    {
        std::string value = std::to_string(id);
        if (bans.size() + value.size() + 1 > 200)
        {
            Send(player, "SpellChoiceBans", bans);
            bans.clear();
        }
        if (!bans.empty())
            bans += ',';
        bans += value;
    }
    if (!bans.empty())
        Send(player, "SpellChoiceBans", bans);
    if (!state.stats.Remaining() || !ValidOffer(player, state))
    {
        Send(player, "SpellChoiceClose", "");
        return;
    }
    Send(player, "SpellChoice", Choices(state.stats));
    std::string rarities;
    for (uint32 id : state.stats.offered)
    {
        if (!rarities.empty())
            rarities += ',';
        rarities += std::to_string(_rarities.at(id));
    }
    Send(player, "SpellChoiceRarities", rarities);
}

bool HabilidadesRandomMgr::Grant(Player* player, uint32 id, std::unordered_set<uint32>& added)
{
    // Track only actual additions, including triggered/auxiliary spells. Never claim a pre-existing rank.
    std::unordered_set<uint32> before;
    for (auto const& pair : player->GetSpellMap())
        if (pair.second->State != PLAYERSPELL_REMOVED)
            before.insert(pair.first);
    auto& state = _players.at(Guid(player));
    bool transitioning = state.transitioning;
    state.transitioning = true;
    player->learnSpell(id);
    for (auto const& pair : player->GetSpellMap())
        if (pair.second->State != PLAYERSPELL_REMOVED && !before.count(pair.first))
            added.insert(pair.first);
    state.transitioning = transitioning;
    return player->HasSpell(id);
}

bool HabilidadesRandomMgr::UpgradeDrafted(Player* player)
{
    auto& state = _players.at(Guid(player));
    if (!IsEnabled() || state.completed || _recoveryRequired.count(Guid(player)))
        return false;
    auto next = state;
    std::unordered_set<uint32> roots;
    for (uint32 id : state.drafted)
        roots.insert(Root(id));
    for (uint32 root : roots)
        for (uint32 id = root; id; id = sSpellMgr->GetNextSpellInChain(id))
        {
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(id);
            if (spell && spell->SpellLevel <= player->GetLevel() && !player->HasSpell(id))
                Grant(player, id, next.drafted);
        }
    if (next.drafted == state.drafted)
        return true;
    if (!Save(player, next, true))
    {
        for (uint32 learned : next.drafted)
            if (!state.drafted.count(learned))
                player->removeSpell(learned, SPEC_MASK_ALL, false);
        return false;
    }
    state = std::move(next);
    return true;
}

void HabilidadesRandomMgr::Select(Player* player, uint32 id)
{
    auto& state = _players.at(Guid(player));
    if (!state.stats.CanSelect(id, state.bans.count(id)) || !ValidOffer(player, state))
    {
        Check(player);
        return;
    }
    auto next = state;
    if (!Grant(player, id, next.drafted))
    {
        for (uint32 learned : next.drafted)
            if (!state.drafted.count(learned))
                player->removeSpell(learned, SPEC_MASK_ALL, false);
        Notice(player, "[Desafío] No se pudo aprender ese poder. No se consumió la elección.");
        return;
    }
    if (id == 47241)
        for (uint32 auxiliary : {50581u,59671u,54785u,50589u})
            Grant(player, auxiliary, next.drafted);
    ++next.stats.successful;
    next.stats.offered = {};
    if (!Save(player, next, true))
    {
        for (uint32 learned : next.drafted)
            if (!state.drafted.count(learned))
                player->removeSpell(learned, SPEC_MASK_ALL, false);
        return;
    }
    state = std::move(next);
    UpgradeDrafted(player);
    Send(player, "SpellChoiceClose", "");
    Check(player);
}

void HabilidadesRandomMgr::Reroll(Player* player)
{
    auto next = _players.at(Guid(player));
    if (!ValidOffer(player, next))
    {
        Check(player);
        return;
    }
    std::unordered_set<uint32> old(next.stats.offered.begin(), next.stats.offered.end());
    auto replacements = RandomSpells(player, 3, old);
    if (!next.stats.CanReroll(replacements.size() == 3))
    {
        Send(player, "SpellChoiceRerollDenied", "0");
        return;
    }
    --next.stats.rerolls;
    std::copy(replacements.begin(), replacements.end(), next.stats.offered.begin());
    if (Save(player, next))
        _players.at(Guid(player)) = std::move(next);
    Check(player);
}

void HabilidadesRandomMgr::Ban(Player* player, uint32 id)
{
    auto next = _players.at(Guid(player));
    if (!ValidOffer(player, next))
    {
        Check(player);
        return;
    }
    std::unordered_set<uint32> old(next.stats.offered.begin(), next.stats.offered.end());
    auto replacements = RandomSpells(player, 1, old);
    if (!next.stats.CanBan(id, next.bans.count(id), !replacements.empty()))
    {
        Send(player, "SpellChoiceBanDenied", "0");
        return;
    }
    --next.stats.bans;
    next.bans.insert(id);
    std::replace(next.stats.offered.begin(), next.stats.offered.end(), id, replacements[0]);
    if (Save(player, next))
    {
        _players.at(Guid(player)) = std::move(next);
        Send(player, "SpellChoiceBanAccepted", std::to_string(id));
    }
    Check(player);
}

bool HabilidadesRandomMgr::HandleWhisper(Player* player, std::string const& message)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_recoveryRequired.count(Guid(player)))
        return true;
    if (message == "SC_CHECK" || message == "DRAFT_READY")
    {
        if (IsParticipant(player))
        {
            auto& state = _players.at(Guid(player));
            auto now = std::chrono::steady_clock::now();
            if (now - state.lastRequest < std::chrono::milliseconds(300))
                return true;
            state.lastRequest = now;
        }
        Check(player);
        return true;
    }
    if (!IsEnabled() || !IsParticipant(player) || player->GetLevel() >= 80)
        return true;
    auto& state = _players.at(Guid(player));
    auto now = std::chrono::steady_clock::now();
    if (now - state.lastRequest < std::chrono::milliseconds(300))
        return true;
    state.lastRequest = now;
    if (message == "SC_REROLL")
        Reroll(player);
    else if (message == "SC_REPLACE_BANNED")
        Check(player); // Server already replaced it atomically.
    else
    {
        size_t offset = message.rfind("SC_BAN:", 0) == 0 ? 7 : message.rfind("SC:", 0) == 0 ? 3 : 0;
        if (!offset)
            return false;
        uint32 id = 0;
        auto parsed = std::from_chars(message.data() + offset, message.data() + message.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != message.data() + message.size() || !id)
            return true;
        if (offset == 7)
            Ban(player, id);
        else
            Select(player, id);
    }
    return true;
}
