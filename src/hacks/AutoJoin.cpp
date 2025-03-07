/*
 * AutoJoin.cpp
 *
 *  Created on: Jul 28, 2017
 *      Author: nullifiedcat
 *  Credits to cinortaxeH for fixing autojoin
 * 
 */

#include <settings/Int.hpp>
#include "HookTools.hpp"
#include <hacks/AutoJoin.hpp>

#include "common.hpp"
#include "hack.hpp"
#include "MiscTemporary.hpp"

namespace hacks::shared::autojoin
{
static settings::Boolean autojoin_team{ "autojoin.team", "true" };
static settings::Boolean random_class{ "autojoin.random-class", "false" };
static settings::Int autojoin_class{ "autojoin.class", "0" };
static settings::Boolean auto_queue{ "autojoin.auto-queue", "false" };
static settings::Boolean auto_requeue{ "autojoin.auto-requeue", "false" };

/*
 * Credits to Blackfire for helping me with auto-requeue!
 */

const std::string class_names[] = { "scout", "sniper", "soldier", "demoman", "medic", "heavyweapons", "pyro", "spy", "engineer" };

bool UnassignedTeam()
{
    return !g_pLocalPlayer->team || g_pLocalPlayer->team == TEAM_SPEC;
}

bool UnassignedClass()
{
    return g_pLocalPlayer->clazz != *autojoin_class;
}

static Timer startqueue_timer{};
#if !ENABLE_VISUALS
static Timer queue_timer{};
#endif

void UpdateSearch()
{
    if (!*auto_queue && !*auto_requeue || g_IEngine->IsInGame())
    {
#if !ENABLE_VISUALS
        queue_timer.update();
#endif
        return;
    }

    static uintptr_t addr    = CSignature::GetClientSignature("C7 04 24 ? ? ? ? 8D 7D ? 31 F6");
    static auto offset0      = uintptr_t(*(uintptr_t *) (addr + 0x3));
    static uintptr_t offset1 = CSignature::GetClientSignature("55 89 E5 83 EC ? 8B 45 ? 8B 80 ? ? ? ? 85 C0 74 ? C7 44 24 ? ? ? ? ? 89 04 24 E8 ? ? ? ? 85 C0 74 ? 8B 40");
    typedef int (*GetPendingInvites_t)(uintptr_t);
    auto GetPendingInvites = GetPendingInvites_t(offset1);
    int invites            = GetPendingInvites(offset0);

    re::CTFGCClientSystem *gc = re::CTFGCClientSystem::GTFGCClientSystem();
    re::CTFPartyClient *pc    = re::CTFPartyClient::GTFPartyClient();

    if (current_user_cmd && gc && gc->BConnectedToMatchServer(false) && gc->BHaveLiveMatch())
    {
#if !ENABLE_VISUALS
        queue_timer.update();
#endif
        tfmm::leaveQueue();
    }
    //    if (gc && !gc->BConnectedToMatchServer(false) &&
    //            queuetime.test_and_set(10 * 1000 * 60) &&
    //            !gc->BHaveLiveMatch())
    //        tfmm::leaveQueue();

    if (*auto_requeue && startqueue_timer.check(5000) && gc && !gc->BConnectedToMatchServer(false) && !gc->BHaveLiveMatch() && !invites && pc && !(pc->BInQueueForMatchGroup(tfmm::getQueue()) || pc->BInQueueForStandby()))
    {
        logging::Info("Starting queue for standby");
        tfmm::startQueueStandby();
    }

    if (*auto_queue && startqueue_timer.check(5000) && gc && !gc->BConnectedToMatchServer(false) && !gc->BHaveLiveMatch() && !invites && pc && !(pc->BInQueueForMatchGroup(tfmm::getQueue()) || pc->BInQueueForStandby()))
    {
        logging::Info("Starting queue");
        tfmm::startQueue();
    }
    startqueue_timer.test_and_set(5000);
#if !ENABLE_VISUALS
    if (queue_timer.test_and_set(600000))
        g_IEngine->ClientCmd_Unrestricted("quit"); // lol
#endif
}

static Timer join_timer{};

static void Update()
{
    if (join_timer.test_and_set(1000))
    {
        if (*autojoin_team && UnassignedTeam())
        {
            g_IEngine->ServerCmd("team_ui_setup");
            g_IEngine->ServerCmd("menuopen");
        }
        else if (*autojoin_class && UnassignedClass() && *autojoin_class < 10)
        {
            g_IEngine->ServerCmd(format("joinclass ", class_names[*autojoin_class - 1]).c_str());
            g_IEngine->ServerCmd("menuclosed");
        }
        else if (*random_class && UnassignedClass())
        {
            g_IEngine->ServerCmd(format("joinclass random").c_str());
            g_IEngine->ServerCmd("menuclosed");
        }
    }
}

void onShutdown()
{
    if (*auto_queue)
    {
        logging::Info("Starting queue");
        tfmm::startQueue();
    }
}

static CatCommand get_steamid("print_steamid", "Prints your SteamID", []() { g_ICvar->ConsoleColorPrintf(MENU_COLOR, "%u\n", g_ISteamUser->GetSteamID().GetAccountID()); });

static InitRoutine init(
    []()
    {
        EC::Register(EC::CreateMove, Update, "cm_autojoin", EC::average);
        EC::Register(EC::Paint, UpdateSearch, "paint_autojoin", EC::average);
        EC::Register(EC::Shutdown, onShutdown, "shutdown_autojoin", EC::average);
    });
} // namespace hacks::shared::autojoin