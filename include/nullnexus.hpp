#pragma once
#include "config.h"

#ifndef ENABLE_NULLNEXUS
#define ENABLE_NULLNEXUS 0
#endif

#include <string>

namespace nullnexus
{
void init();
void shutdown();
void update();
void sendChat(const std::string &message);
void sendTeamchat(const std::string &message);
void sendGift(unsigned steamid, unsigned itemid);
void sendKillstreak(unsigned killstreak);
void sendUserMessage(const std::string &name, const std::string &message);
void processChatMessage(const std::string &name, const std::string &message);
void processKillstreak(const std::string &name, int killstreak);
void processGift(const std::string &name, const std::string &target, const std::string &itemname);
void processUserMessage(const std::string &name, const std::string &message, const std::string &channel);
}
