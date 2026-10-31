/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BWLHelpers.h"
#include "AiObjectContext.h"
#include "Group.h"
#include "PathGenerator.h"
#include "Playerbots.h"

namespace BlackwingLairHelpers
{
    static constexpr float ROUTE_END_Z_TOLERANCE = 6.0f;
    static constexpr float ROUTE_MAX_DETOUR = 2.0f;
    static constexpr float ROUTE_DETOUR_SLACK = 30.0f;
    // Two tanks closer than this share a Wyrmguard's Cleave and War Stomp.
    static constexpr float WYRMGUARD_SPLIT_DISTANCE = 10.0f;
    static constexpr float DRAKE_SEARCH_RANGE = 80.0f;
    // A step that ends inside this is a step towards pulling the drake too.
    static constexpr float DRAKE_SAFE_DISTANCE = 35.0f;

    bool IsActiveSuppressionDeviceInRange(GameObject const* go, Player const* bot)
    {
        constexpr float suppressionDeviceInteractionDistance = 15.0f;
        return go &&
               go->GetEntry() == static_cast<uint32>(BlackwingLairGameObjects::GO_SUPPRESSION_DEVICE) &&
               go->GetDistance(bot) < suppressionDeviceInteractionDistance &&
               go->GetGoState() == GO_STATE_READY;
    }

    bool AreRazorgoreEggsAlive(PlayerbotAI* botAI)
    {
        GuidVector gos = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest game objects")->Get();
        for (auto const& guid : gos)
        {
            GameObject const* go = botAI->GetGameObject(guid);
            if (go && go->GetEntry() == static_cast<uint32>(BlackwingLairGameObjects::GO_BLACK_DRAGON_EGG))
                return true;
        }
        return false;
    }

    bool IsRazorgoreOffTank(Player* bot)
    {
        return PlayerbotAI::IsAssistTankOfIndex(bot, 0, true);
    }

    bool IsNonBABotNearPosition(Player const* bot, Position const& position, float distance)
    {
        Group const* group = bot->GetGroup();
        if (!group)
            return false;

        for (GroupReference const* gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player const* p = gref->GetSource();
            if (!p || p == bot || !p->IsAlive() || p->GetMapId() != bot->GetMapId())
                continue;

            if (p->HasAura(static_cast<uint32>(BlackwingLairSpells::SPELL_BURNING_ADRENALINE)))
                continue;

            if (p->GetDistance2d(position.GetPositionX(), position.GetPositionY()) < distance)
                return true;
        }

        return false;
    }

    bool IsCorruptedWhelp(Unit const* unit)
    {
        if (!unit)
            return false;

        const uint32 entry = unit->GetEntry();
        return entry == static_cast<uint32>(BlackwingLairNPCs::NPC_CORRUPTED_RED_WHELP) ||
            entry == static_cast<uint32>(BlackwingLairNPCs::NPC_CORRUPTED_GREEN_WHELP) ||
            entry == static_cast<uint32>(BlackwingLairNPCs::NPC_CORRUPTED_BLUE_WHELP) ||
            entry == static_cast<uint32>(BlackwingLairNPCs::NPC_CORRUPTED_BRONZE_WHELP);
    }

    // BWL's halls run over and under each other, and a mob on the far side can be in combat with the raid. A path that
    // ends on another floor or goes the long way round means the mob is not this bot's to chase, and a player's spline
    // would take the straight line through the wall.
    bool HasNoRouteTo(Player* bot, Unit const* unit)
    {
        if (bot->IsWithinLOSInMap(unit))
            return false;

        PathGenerator path(bot);
        path.CalculatePath(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ());
        if (path.GetPathType() != PATHFIND_NORMAL || path.GetPath().empty())
            return true;

        return std::fabs(path.GetPath().back().z - unit->GetPositionZ()) > ROUTE_END_Z_TOLERANCE ||
               path.getPathLength() > bot->GetDistance(unit) * ROUTE_MAX_DETOUR + ROUTE_DETOUR_SLACK;
    }

    bool IsDeathTalonWyrmguard(Unit const* unit)
    {
        return unit && unit->GetEntry() == static_cast<uint32>(BlackwingLairNPCs::NPC_DEATH_TALON_WYRMGUARD);
    }

    bool IsHeldByTank(Unit const* unit)
    {
        Unit* victim = unit->GetVictim();
        Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
        return victimPlayer && PlayerbotAI::IsTank(victimPlayer);
    }

    std::vector<Unit*> GetWyrmguardAttackers(PlayerbotAI* botAI)
    {
        std::vector<Unit*> wyrmguards;
        for (ObjectGuid const guid : botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get())
        {
            Unit* unit = botAI->GetUnit(guid);
            if (IsDeathTalonWyrmguard(unit) && unit->IsAlive())
                wyrmguards.push_back(unit);
        }
        return wyrmguards;
    }

    // Every Wyrmguard is on a tank, and with a tank for each they are on different tanks and apart.
    bool AreWyrmguardsSplit(Player* bot, std::vector<Unit*> const& wyrmguards)
    {
        std::vector<Unit*> holders;
        for (Unit const* wyrmguard : wyrmguards)
        {
            if (!IsHeldByTank(wyrmguard))
                return false;
            holders.push_back(wyrmguard->GetVictim());
        }

        if (PlayerbotAI::GetGroupTankNum(bot) < wyrmguards.size())
            return true;

        for (size_t i = 0; i < wyrmguards.size(); ++i)
        {
            for (size_t j = i + 1; j < wyrmguards.size(); ++j)
            {
                if (holders[i] == holders[j] ||
                    wyrmguards[i]->GetDistance2d(wyrmguards[j]) < WYRMGUARD_SPLIT_DISTANCE)
                    return false;
            }
        }
        return true;
    }

    std::vector<Unit*> GetUnengagedDrakes(Player* bot)
    {
        std::vector<Unit*> drakes;
        for (BlackwingLairNPCs const npc : { BlackwingLairNPCs::NPC_FIREMAW, BlackwingLairNPCs::NPC_FLAMEGOR,
                                             BlackwingLairNPCs::NPC_EBONROC, BlackwingLairNPCs::NPC_CHROMAGGUS })
        {
            Creature* drake = bot->FindNearestCreature(static_cast<uint32>(npc), DRAKE_SEARCH_RANGE);
            if (drake && !drake->IsInCombat())
                drakes.push_back(drake);
        }
        return drakes;
    }

    bool IsNearUnengagedDrake(std::vector<Unit*> const& drakes, float x, float y)
    {
        for (Unit const* drake : drakes)
        {
            if (drake->GetDistance2d(x, y) < DRAKE_SAFE_DISTANCE)
                return true;
        }
        return false;
    }
}
