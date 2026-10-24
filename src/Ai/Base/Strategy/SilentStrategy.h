/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SILENTSTRATEGY_H
#define PLAYERBOTS_SILENTSTRATEGY_H

#include "Strategy.h"

class PlayerbotAI;

// Keeps a bot from whispering its master unprompted. It still answers whatever a player asks it, and SayAction
// stays quiet too. The checks are in PlayerbotAI::IsTellAllowed and SayAction::isUseful.
class SilentStrategy : public Strategy
{
public:
    SilentStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "silent"; }
};

#endif
