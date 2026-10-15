#ifndef MOD_HABILIDADES_RANDOM_H
#define MOD_HABILIDADES_RANDOM_H

#include "Common.h"
#include "HRDraftState.h"
#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

enum HRRarity : uint8 { HR_COMMON, HR_UNCOMMON, HR_RARE, HR_EPIC, HR_LEGENDARY, HR_BROKEN, HR_PROTECTED = 99 };

struct HRSavedSpell
{
    uint32 id;
    uint8 specMask;
};

struct HRSavedSkill
{
    uint16 id;
    uint16 step;
    uint16 value;
    uint16 maximum;
};

struct HRPlayerState
{
    bool completed = false;
    bool characterPending = false;
    uint8 completionLevel = 0;
    uint64 revision = 0;
    uint32 updateTimer = 0;
    bool transitioning = false;
    bool runesInitialized = false;
    bool hasSkillSnapshot = false;
    uint8 originalClass = 0;
    uint8 originalSpec = 0;
    uint8 originalSpecs = 1;
    std::array<std::array<uint32, 6>, 2> glyphs{};
    HRStats stats;
    std::vector<HRSavedSpell> spells;
    std::vector<HRSavedSpell> talents;
    std::vector<HRSavedSkill> skills;
    std::unordered_set<uint32> drafted;
    std::unordered_set<uint32> bans;
    std::unordered_set<uint32> rejectedLearned;
    std::chrono::steady_clock::time_point lastRequest{};
};

class HabilidadesRandomMgr
{
public:
    static HabilidadesRandomMgr* instance();
    void LoadConfig();
    void Initialize();
    bool IsEnabled() const
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _enabled && _ready;
    }
    bool IsParticipant(Player const* player) const;
    bool TalentsLocked(Player const* player) const;
    bool CanLearnSpell(Player const* player, uint32 id) const;
    bool TrainersBlocked(Player const* player) const;
    void OnLearnSpell(Player* player, uint32 id);
    bool HasHybridPower(Player const* player, uint8 power) const;
    void OnLogin(Player* player);
    void OnLogout(Player* player);
    void OnLevelChanged(Player* player, uint8 oldLevel);
    void OnSpecChanged(Player* player);
    void Update(Player* player, uint32 diff);
    bool Join(Player* player);
    bool Restore(Player* player);
    void Check(Player* player);
    bool HandleWhisper(Player* player, std::string const& message);

private:
    HabilidadesRandomMgr();
    bool LoadPlayer(Player* player);
    bool Save(Player* player, HRPlayerState& state, bool savePlayer = false);
    bool SaveCharacter(Player* player);
    void RequireRecovery(Player* player);
    void Suspend(Player* player);
    void SuspendTalents(Player* player);
    bool Generate(Player* player);
    bool UpgradeDrafted(Player* player);
    bool Grant(Player* player, uint32 id, std::unordered_set<uint32>& added);
    void Select(Player* player, uint32 id);
    void Reroll(Player* player);
    void Ban(Player* player, uint32 id);
    bool ValidOffer(Player* player, HRPlayerState const& state) const;
    std::vector<uint32> RandomSpells(Player* player, uint32 count,
        std::unordered_set<uint32> const& exclude) const;
    void Send(Player* player, std::string const& prefix, std::string const& message) const;
    void Close(Player* player) const;
    bool IsProtected(uint32 id) const;
    bool ShouldSuspend(uint32 id) const;
    uint32 Root(uint32 id) const;

    mutable std::recursive_mutex _mutex;
    bool _enabled = true;
    bool _ready = false;
    bool _includeBroken = false;
    void ChangeClass(Player* player, uint8 classId);
    bool Eligible(uint32 id, Player const* player) const;
    bool _blockTrainerSpells = true;
    uint32 _initialDrafts = 1;
    uint32 _draftsPerLevel = 1;
    uint32 _initialRerolls = 5;
    uint32 _rerollsPerLevel = 2;
    uint32 _initialBans = 5;
    uint32 _poolAmount = 45;
    std::array<float, 6> _distribution = {.50f, .27f, .14f, .06f, .03f, .01f};
    std::unordered_map<uint32, uint8> _rarities;
    std::unordered_set<uint32> _protected;
    std::unordered_map<uint32, HRPlayerState> _players;
    std::unordered_set<uint32> _recoveryRequired;
};

#define sHabilidadesRandomMgr HabilidadesRandomMgr::instance()
#endif
