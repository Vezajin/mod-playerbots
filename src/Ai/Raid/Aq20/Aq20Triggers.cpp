/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq20Triggers.h"
#include "Aq20Utils.h"

namespace
{
constexpr float MOAM_DRAIN_MANA_PCT = 25.0f;
}  // namespace

bool Aq20MoveToCrystalTrigger::IsActive()
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "ossirian the unscarred"))
    {
        if (boss->IsInCombat())
        {
            // if buff is active move to crystal
            if (RaidAq20Utils::IsOssirianBuffActive(boss))
                return true;

            // if buff is not active a debuff will be, buff becomes active once debuff expires
            // so move to crystal when debuff almost done, or based debuff time left and
            // distance bot is from crystal (ie: start moving early enough to make it)
            int32 debuffTimeRemaining = RaidAq20Utils::GetOssirianDebuffTimeRemaining(boss);
            if (debuffTimeRemaining < 5000)
                return true;
            if (debuffTimeRemaining < 30000)
            {
                if (GameObject* crystal = RaidAq20Utils::GetNearestCrystal(boss))
                {
                    float botDist = bot->GetDistance(crystal);
                    float timeToReach = botDist / bot->GetSpeed(MOVE_RUN);
                    // bot should ideally activate crystal a ~5 seconds early (due to time it takes for crystal
                    // to activate) so aim to get there in time to do so
                    return debuffTimeRemaining - 5 * IN_MILLISECONDS < timeToReach * IN_MILLISECONDS;
                }
            }
        }
    }
    return false;
}

bool Aq20MoamHasManaTrigger::IsActive()
{
    // Moam drains the raid's mana and casts Arcane Eruption whenever his own is full, stone phase included. The
    // DPS that can burn or drain mana keep it low; healers keep healing.
    if ((!bot->IsClass(CLASS_PRIEST) && !bot->IsClass(CLASS_HUNTER) && !bot->IsClass(CLASS_WARLOCK)) ||
        PlayerbotAI::IsHeal(bot))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "moam");
    return boss && boss->GetPowerPct(POWER_MANA) >= MOAM_DRAIN_MANA_PCT;
}
