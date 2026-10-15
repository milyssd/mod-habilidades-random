#include "HabilidadesRandom.h"
#include "AccountScript.h"
#include "Chat.h"
#include "Creature.h"
#include "Item.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"

class HRWorld : public WorldScript
{
public:
    HRWorld() : WorldScript("HabilidadesRandomWorld") { }
    void OnAfterConfigLoad(bool) override { sHabilidadesRandomMgr->LoadConfig(); }
    void OnStartup() override { sHabilidadesRandomMgr->Initialize(); }
};

class HRPlayer : public PlayerScript
{
public:
    HRPlayer() : PlayerScript("HabilidadesRandomPlayer") { }
    void OnPlayerLogin(Player* player) override { sHabilidadesRandomMgr->OnLogin(player); }
    void OnPlayerLogout(Player* player) override { sHabilidadesRandomMgr->OnLogout(player); }
    void OnPlayerAfterUpdate(Player* player, uint32 diff) override
    {
        sHabilidadesRandomMgr->Update(player, diff);
    }
    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        sHabilidadesRandomMgr->OnLevelChanged(player, oldLevel);
    }
    void OnPlayerAfterSpecSlotChanged(Player* player, uint8) override
    {
        sHabilidadesRandomMgr->OnSpecChanged(player);
    }
    void OnPlayerCalculateTalentsPoints(Player const* player, uint32& points) override
    {
        if (sHabilidadesRandomMgr->TalentsLocked(player))
            points = 0;
    }
    bool OnPlayerCanLearnTalent(Player* player, TalentEntry const*, uint32) override
    {
        return !sHabilidadesRandomMgr->TalentsLocked(player);
    }
    bool OnPlayerCanLearnSpell(Player* player, uint32 id) override
    {
        return sHabilidadesRandomMgr->CanLearnSpell(player, id);
    }
    bool OnPlayerHasActivePowerType(Player const* player, Powers power) override
    {
        return sHabilidadesRandomMgr->HasHybridPower(player, uint8(power));
    }
    bool OnPlayerCanCastItemUseSpell(Player* player, Item* item, SpellCastTargets const&, uint8, uint32) override
    {
        // Original glyphs remain stored but inactive; do not replace them during the challenge.
        return !sHabilidadesRandomMgr->IsParticipant(player) || item->GetTemplate()->Class != ITEM_CLASS_GLYPH;
    }
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& message, Player* receiver) override
    {
        bool command = message == "DRAFT_READY" || message == "SC_CHECK" || message == "SC_REROLL" ||
            message == "SC_REPLACE_BANNED" || message.rfind("SC_BAN:", 0) == 0 || message.rfind("SC:", 0) == 0;
        // Only self whispers are a control channel. Messages to another player are not actions.
        if (!command || type != CHAT_MSG_WHISPER || language == LANG_ADDON || receiver != player)
            return true;
        sHabilidadesRandomMgr->HandleWhisper(player, message);
        return false;
    }
};

class HRCharacterCreation : public AccountScript
{
public:
    HRCharacterCreation() : AccountScript("HabilidadesRandomCharacterCreation") { }
    bool CanAccountCreateCharacter(uint32, uint8 race, uint8 classId) override
    {
        // Compatibility rows are for enrolled characters, not for creating permanent custom classes.
        return classId != CLASS_DRUID || race == RACE_NIGHTELF || race == RACE_TAUREN;
    }
};

class HRChallengeNpc : public CreatureScript
{
public:
    HRChallengeNpc() : CreatureScript("npc_habilidades_random_event") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ClearGossipMenuFor(player);
        if (sHabilidadesRandomMgr->IsParticipant(player))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Ver mis elecciones pendientes", GOSSIP_SENDER_MAIN, 3);
        else if (sHabilidadesRandomMgr->IsEnabled() && player->GetLevel() < 80)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Conocer el desafío de leveo hasta el 80", GOSSIP_SENDER_MAIN, 1);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        ClearGossipMenuFor(player);
        if (sender != GOSSIP_SENDER_MAIN)
            return false;
        if (action == 1)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Aceptar: druida temporal, poderes aleatorios y sin talentos hasta el 80",
                GOSSIP_SENDER_MAIN, 2,
                "Se guardarán tu clase, habilidades y talentos. Conservarás tu raza. Al nivel 80 recuperarás tus poderes "
                "originales y podrás volver a usar entrenadores. El desafío dura hasta el 80. ¿Aceptar?", 0, false);
            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return true;
        }
        CloseGossipMenuFor(player);
        if (action == 2)
            sHabilidadesRandomMgr->Join(player);
        else if (action == 3)
            sHabilidadesRandomMgr->Check(player);
        return true;
    }
};

void AddHabilidadesRandomScripts()
{
    new HRWorld();
    new HRCharacterCreation();
    new HRPlayer();
    new HRChallengeNpc();
}
