#include "HabilidadesRandom.h"
#include "HRCharacterSave.h"
#include "HRDatabase.h"
#include "HRRuntime.h"
#include "HRLearnSpellHook.h"
#include "AllCreatureScript.h"
#include "CommandScript.h"
#include "Config.h"
#include "Log.h"
#include "RBAC.h"
#include "World.h"
#include "AccountScript.h"
#include "Chat.h"
#include "Creature.h"
#include "Item.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "ServerScript.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "Trainer.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
bool IsClassTrainer(Creature const* creature)
{
    if (!creature)
        return false;
    Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(creature->GetEntry());
    return creature->HasNpcFlag(UNIT_NPC_FLAG_TRAINER_CLASS) ||
        (trainer && trainer->GetTrainerType() == Trainer::Type::Class);
}
}

class HRWorld : public WorldScript
{
public:
    HRWorld() : WorldScript("HabilidadesRandomWorld") { }
    void OnAfterConfigLoad(bool reload) override
    {
        sHabilidadesRandomMgr->LoadConfig();
        if (reload && sConfigMgr->GetOption<bool>("HabilidadesRandom.Enable", true) &&
            (!_storageReady || !_profilesReady || !_npcReady))
            LOG_INFO("server.loading", "HabilidadesRandom: reinicia worldserver para activar el evento tras corregir su configuración o sus parches.");
    }
    void OnBeforeWorldInitialized() override
    {
        _storageReady = false;
        _npcReady = false;
        // Profiles are memory-only and keep converted races visible for recovery.
        _profilesReady = HRRuntime::PrepareProfiles();
        if (!sConfigMgr->GetOption<bool>("HabilidadesRandom.Enable", true))
        {
            sHabilidadesRandomMgr->SetAvailability(false, _profilesReady, false);
            LOG_INFO("server.loading", "HabilidadesRandom: desactivado; no se abre la base privada ni se crean NPC.");
            return;
        }
        if (!_profilesReady)
        {
            sHabilidadesRandomMgr->SetAvailability(false, false, false);
            LOG_ERROR("server.loading", "HabilidadesRandom: perfiles no disponibles; comprueba datos y parches. El evento queda inactivo y worldserver continúa. Desde la raíz del core ejecuta python modules/mod-habilidades-random/tools/prepare_core.py --core . y recompila.");
            return;
        }
        _storageReady = HRDatabaseStore.Open(sConfigMgr->GetOption<std::string>("HabilidadesRandom.DatabaseInfo", ""),
            sConfigMgr->GetOption<std::string>("HabilidadesRandom.DatabaseName", "mod_habilidades_random"));
        if (!_storageReady)
        {
            sHabilidadesRandomMgr->SetAvailability(false, _profilesReady, false);
            LOG_ERROR("server.loading", "HabilidadesRandom: base privada no disponible. El evento queda inactivo y worldserver continúa.");
            return;
        }
        _npcReady = HRRuntime::Prepare();
        sHabilidadesRandomMgr->SetAvailability(_storageReady, _profilesReady, _npcReady);
        if (!_npcReady)
            LOG_ERROR("server.loading", "HabilidadesRandom: NPC no disponible; inscripciones cerradas. Se conserva la recuperación de participantes y worldserver continúa.");
    }
    void OnUpdate(uint32 diff) override
    {
        if (!HRDatabaseStore.IsOpen())
            return;
        if (diff < _keepAliveTimer)
        {
            _keepAliveTimer -= diff;
            return;
        }
        _keepAliveTimer = 60000;
        HRDatabaseStore.KeepAlive();
    }
    void OnAfterUnloadAllMaps() override
    {
        // Wait until player/map callbacks have stopped, then close our own connection.
        HRCloseCharacterSave();
        HRDatabaseStore.Close();
        sHabilidadesRandomMgr->SetAvailability(false, false, false);
    }
    void OnStartup() override
    {
        if (!_storageReady || !_profilesReady)
            return;
        sHabilidadesRandomMgr->Initialize();
        if (sHabilidadesRandomMgr->IsEnabled())
            HRRuntime::LoadNpcs();
    }
private:
    bool _storageReady = false;
    bool _profilesReady = false;
    bool _npcReady = false;
    uint32 _keepAliveTimer = 60000;
};

struct HRLearnSpellCallbacks
{
    static bool BeforeLearn(Player* player, uint32 id)
    {
        return sHabilidadesRandomMgr->CanLearnSpell(player, id);
    }
    static void AfterLearn(Player* player, uint32 id)
    {
        sHabilidadesRandomMgr->OnLearnSpell(player, id);
    }
};

using HRPlayerBase = HRLearnSpellHook<PlayerScript, HRLearnSpellCallbacks>;

class HRPlayer : public HRPlayerBase
{
public:
    HRPlayer() : HRPlayerBase("HabilidadesRandomPlayer") { }
    void OnPlayerLogin(Player* player) override { sHabilidadesRandomMgr->OnLogin(player); }
    void OnPlayerLogout(Player* player) override { sHabilidadesRandomMgr->OnLogout(player); }
    void OnPlayerBeforeUpdate(Player* player, uint32 diff) override
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
        return classId != CLASS_DRUID || !HRRuntime::IsEventOnlyDruidRace(race);
    }
};

class HRChallengeNpc : public AllCreatureScript
{
public:
    HRChallengeNpc() : AllCreatureScript("HabilidadesRandomNpc") { }

    bool CanCreatureGossipHello(Player* player, Creature* creature) override
    {
        if (!HRRuntime::IsNpc(creature))
        {
            if (IsClassTrainer(creature) && sHabilidadesRandomMgr->TrainersBlocked(player))
            {
                CloseGossipMenuFor(player);
                ChatHandler(player->GetSession()).SendSysMessage("[Desafío] Los entrenadores de clase están bloqueados hasta completar el modo.");
                return true;
            }
            return false;
        }
        ClearGossipMenuFor(player);
        if (sHabilidadesRandomMgr->IsParticipant(player))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Ver mis elecciones pendientes", GOSSIP_SENDER_MAIN, 3);
        else if (sHabilidadesRandomMgr->IsEnabled() && player->GetLevel() < 80)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Conocer el desafío de leveo hasta el 80", GOSSIP_SENDER_MAIN, 1);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool CanCreatureGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (!HRRuntime::IsNpc(creature))
            return false;
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

class HRTrainerRequests : public ServerScript
{
public:
    HRTrainerRequests() : ServerScript("HabilidadesRandomTrainerRequests") { }
    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        // Trainer packets can bypass GossipHello. Reject them before the core deducts money.
        if ((packet.GetOpcode() != CMSG_TRAINER_LIST && packet.GetOpcode() != CMSG_TRAINER_BUY_SPELL) ||
            !session || !session->GetPlayer() || !sHabilidadesRandomMgr->TrainersBlocked(session->GetPlayer()))
            return true;
        if (packet.size() < sizeof(uint64))
            return true; // Let the normal packet parser handle malformed requests.
        WorldPacket copy(packet);
        copy.rpos(0);
        ObjectGuid guid;
        copy >> guid;
        Creature* creature = session->GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_TRAINER);
        if (!IsClassTrainer(creature))
            return true;
        ChatHandler(session).SendSysMessage("[Desafío] Los entrenadores de clase están bloqueados hasta completar el modo.");
        return false;
    }
};

using namespace Acore::ChatCommands;

class HRNpcCommands : public CommandScript
{
public:
    HRNpcCommands() : CommandScript("HabilidadesRandomNpcCommands") { }
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commands = {
            {"npc", Place, rbac::RBAC_PERM_COMMAND_NPC_ADD, Console::No},
            {"removenpc", Remove, rbac::RBAC_PERM_COMMAND_NPC_ADD, Console::No}
        };
        static ChatCommandTable root = {{"hr", commands}};
        return root;
    }
    static bool Place(ChatHandler* handler)
    {
        if (!HRRuntime::PlaceNpc(handler->GetPlayer()))
        {
            handler->SendSysMessage("No se pudo crear el NPC. Solo continentes normales; comprobar la base propia y el parche del core.");
            return false;
        }
        handler->SendSysMessage("NPC creado y guardado en la base del evento. No se escribió ningún spawn en world.");
        return true;
    }
    static bool Remove(ChatHandler* handler)
    {
        return HRRuntime::RemoveNpc(handler->getSelectedCreature());
    }
};

void AddHabilidadesRandomScripts()
{
    new HRNpcCommands();
    new HRWorld();
    new HRCharacterCreation();
    new HRPlayer();
    new HRChallengeNpc();
    new HRTrainerRequests();
}
