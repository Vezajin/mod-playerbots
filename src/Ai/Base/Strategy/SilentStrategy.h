/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SILENTSTRATEGY_H
#define PLAYERBOTS_SILENTSTRATEGY_H

#include "Strategy.h"

class PlayerbotAI;

// Keeps a bot from talking unprompted: no whispers to its master and nothing it says, yells or sends to party or
// raid chat through PlayerbotAI. It still answers the chat commands a player sends it. The checks are in
// PlayerbotAI::IsSilenced and SayAction::isUseful.
class SilentStrategy : public Strategy
{
public:
    SilentStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "silent"; }
    bool SilencesChat() const override { return true; }
};

#endif
