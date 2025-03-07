/*
  Created by Jenny White on 29.04.18.
  Copyright (c) 2018 nullworks. All rights reserved.
*/

#include <MiscTemporary.hpp>
#include <settings/Int.hpp>
#include "AntiAim.hpp"
#include "HookedMethods.hpp"
#include <MiscTemporary.hpp>
#include "e8call.hpp"
#include "Warp.hpp"
#include "nospread.hpp"
#include "AntiCheatBypass.hpp"
#include <boost/algorithm/string.hpp>

static settings::Int newlines_msg{ "chat.prefix-newlines", "0" };
static settings::Boolean log_sent{ "debug.log-sent-chat", "false" };
static settings::Boolean answerIdentify{ "chat.identify.answer", "true" };
static Timer identify_timer{};
constexpr int CAT_IDENTIFY   = 0xCA7;
constexpr int CAT_REPLY      = 0xCA8;
constexpr float AUTH_MESSAGE = 1234567.0f;

namespace hacks::shared::catbot
{
void SendNetMsg(INetMessage &msg);
}
namespace hooked_methods
{

static bool send_achievement_reply{};
static Timer send_achievement_reply_timer{};

// Welcome back Achievement based identify.
void sendAchievementKv(int value)
{
    KeyValues *kv = new KeyValues("AchievementEarned");
    kv->SetInt("achievementID", value);
    g_IEngine->ServerCmdKeyValues(kv);
}

// Goodbye old Friend.
/*void sendDrawlineKv(float x_value, float y_value)
{
    KeyValues *kv = new KeyValues("cl_drawline");
    kv->SetInt("panel", 2);
    kv->SetInt("line", 0);
    kv->SetFloat("x", x_value);
    kv->SetFloat("y", y_value);
    g_IEngine->ServerCmdKeyValues(kv);
}*/

void sendIdentifyMessage(bool reply)
{
    reply ? sendAchievementKv(CAT_REPLY) : sendAchievementKv(CAT_IDENTIFY);
    /*reply ? sendDrawlineKv(CAT_REPLY, AUTH_MESSAGE) : sendDrawlineKv(CAT_IDENTIFY, AUTH_MESSAGE);*/
}

static CatCommand debug_drawpanel("debug_drawline", "debug",
                                  []()
                                  {
                                      KeyValues *kv = new KeyValues("cl_drawline");
                                      // Has to be this to get broadcasted
                                      kv->SetInt("panel", 2);
                                      // "New" line
                                      kv->SetInt("line", 0);

                                      kv->SetFloat("x", CAT_IDENTIFY);
                                      kv->SetFloat("y", AUTH_MESSAGE);
                                      g_IEngine->ServerCmdKeyValues(kv);
                                  });

#if ENABLE_TEXTMODE
settings::Boolean identify{ "chat.identify", "true" };
#else
settings::Boolean identify{ "chat.identify", "false" };
#endif

/*void ProcessSendline(IGameEvent *kv)
{
    int player_idx = kv->GetInt("player", 0xDEAD);

    auto id            = kv->GetFloat("x");
    float message_type = kv->GetFloat("y");
    auto panel_type    = kv->GetInt("panel");
    auto line_type     = kv->GetInt("line");

    // Verify all the data matches
    if (player_idx != 0xDEAD && panel_type == 2 && line_type == 0 && message_type == AUTH_MESSAGE && (id == CAT_IDENTIFY || id == CAT_REPLY))
    {
        player_info_s info;
        if (!GetPlayerInfo(player_idx, &info))
            return;
        // CA7 = Reply and change state
        // CA8 = Change state
        if (id == CAT_IDENTIFY && *answerIdentify && player_idx != g_pLocalPlayer->entity_idx && playerlist::AccessData(info.friendsID).state != playerlist::k_EState::RAGE)
            send_drawline_reply = true;
        if (playerlist::ChangeState(info.friendsID, playerlist::k_EState::CAT))
            PrintChat("\x07%06X%s\x01 Marked as CAT (Cathook user)", 0xe05938, info.name);
    }
}*/

std::vector<KeyValues *> Iterate(KeyValues *event, int depth)
{
    std::vector<KeyValues *> peer_list = { event };
    for (int i = 0; i < depth; i++)
    {
        for (auto ev : peer_list)
            for (KeyValues *dat2 = ev; dat2 != NULL; dat2 = dat2->m_pPeer)
                if (std::find(peer_list.begin(), peer_list.end(), dat2) == peer_list.end())
                    peer_list.push_back(dat2);
        for (auto ev : peer_list)
            for (KeyValues *dat2 = ev; dat2 != NULL; dat2 = dat2->m_pSub)
                if (std::find(peer_list.begin(), peer_list.end(), dat2) == peer_list.end())
                    peer_list.push_back(dat2);
    }
    return peer_list;
}

void ParseKeyValue(KeyValues *event)
{
    std::string event_name = event->GetName();
    auto peer_list         = Iterate(event, 10);
    // loop through all our peers
    for (KeyValues *dat : peer_list)
    {
        auto data_type = dat->m_iDataType;
        auto name      = dat->GetName();
        logging::Info("%s", name, data_type);
        switch (dat->m_iDataType)
        {
        case KeyValues::types_t::TYPE_NONE:
        {
            logging::Info("%s is typeless", name);
            break;
        }
        case KeyValues::types_t::TYPE_STRING:
        {
            if (dat->m_sValue && *(dat->m_sValue))
            {
                logging::Info("%s is String: %s", name, dat->m_sValue);
            }
            else
            {
                logging::Info("%s is String: %s", name, "");
            }
            break;
        }
        case KeyValues::types_t::TYPE_WSTRING:
        {
            break;
        }

        case KeyValues::types_t::TYPE_INT:
        {
            logging::Info("%s is int: %d", name, dat->m_iValue);
            break;
        }

        case KeyValues::types_t::TYPE_UINT64:
        {
            logging::Info("%s is double: %f", name, *(double *) dat->m_sValue);
            break;
        }

        case KeyValues::types_t::TYPE_FLOAT:
        {
            logging::Info("%s is float: %f", name, dat->m_flValue);
            break;
        }
        case KeyValues::types_t::TYPE_COLOR:
        {
            logging::Info("%s is Color: { %u %u %u %u}", name, dat->m_Color[0], dat->m_Color[1], dat->m_Color[2], dat->m_Color[3]);
            break;
        }
        case KeyValues::types_t::TYPE_PTR:
        {
            logging::Info("%s is Pointer: %x", name, dat->m_pValue);
            break;
        }

        default:
            break;
        }
    }
}

void ProcessAchievement(IGameEvent *ach)
{
    int player_idx  = ach->GetInt("player", 0xDEAD);
    int achievement = ach->GetInt("achievement", 0xDEAD);
    if (player_idx != 0xDEAD && (achievement == CAT_IDENTIFY || achievement == CAT_REPLY))
    {
        // Always reply and set on CA7 and only set on CA8
        bool reply = achievement == CAT_IDENTIFY;
        player_info_s info;
        if (!g_IEngine->GetPlayerInfo(player_idx, &info))
            return;
        if (reply && *answerIdentify && player_idx != g_pLocalPlayer->entity_idx)
        {
            send_achievement_reply_timer.update();
            send_achievement_reply = true;
        }
        if (playerlist::ChangeState(info.friendsID, playerlist::k_EState::CAT))
            PrintChat("\x07%06X%s\x01 Marked as CAT (Cathook user)", 0xe05938, info.name);
    }
}

class AchievementListener : public IGameEventListener2
{
    virtual void FireGameEvent(IGameEvent *event)
    {
        ProcessAchievement(event);
    }
};
static CatCommand send_identify("debug_send_identify", "debug", []() { sendIdentifyMessage(false); });

static AchievementListener event_listener{};

static InitRoutine run_identify(
    []()
    {
        EC::Register(
            EC::CreateMove,
            []()
            {
                if (send_achievement_reply && send_achievement_reply_timer.check(10000))
                {
                    sendIdentifyMessage(true);
                    send_achievement_reply = false;
                }
                // It is safe to send every 15ish seconds, small packet
                if (!*identify || CE_BAD(LOCAL_E) || !identify_timer.test_and_set(15000))
                    return;
                sendIdentifyMessage(false);
            },
            "sendnetmsg_createmove");
        g_IEventManager2->AddListener(&event_listener, "achievement_earned", false);
        EC::Register(
            EC::Shutdown, []() { g_IEventManager2->RemoveListener(&event_listener); }, "shutdown_event");
    });

DEFINE_HOOKED_METHOD(SendNetMsg, bool, INetChannel *this_, INetMessage &msg, bool force_reliable, bool voice)
{
    if (!isHackActive())
        return original::SendNetMsg(this_, msg, force_reliable, voice);

    size_t say_idx, say_team_idx;
    int offset;
    std::string newlines{};

    // Add Newlines
    if (newlines_msg)
    {
        // Note - 127 is max length of chat message, including the printing chars
        newlines = std::string(*newlines_msg, '\n');
        offset   = newlines.size();
    }

    return original::SendNetMsg(this_, msg, force_reliable, voice);
}
} // namespace hooked_methods
