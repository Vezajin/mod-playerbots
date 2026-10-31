/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BWLHelpers.h"
#include "AiObjectContext.h"
#include "Group.h"
#include "Playerbots.h"
#include <algorithm>
#include <mutex>
#include <set>
#include <unordered_map>

namespace BlackwingLairHelpers
{
    static constexpr uint32 WYRMGUARD_HOLD_LIMIT_MS = 8 * IN_MILLISECONDS;
    static constexpr uint32 WYRMGUARD_PACK_GAP_MS = 5 * IN_MILLISECONDS;

    namespace
    {
        struct WyrmguardHold
        {
            uint32 startedMs = 0;
            uint32 lastCheckMs = 0;
        };

        std::mutex wyrmguardHoldMutex;
        std::unordered_map<uint32, WyrmguardHold> wyrmguardHolds;
    }

    // Two tanks closer than this share a Wyrmguard's Cleave and War Stomp.
    static constexpr float WYRMGUARD_SPLIT_DISTANCE = 10.0f;
    static constexpr float DRAKE_SEARCH_RANGE = 55.0f;
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
            if (IsDeathTalonWyrmguard(unit) && unit->IsAlive() && unit->IsInCombat())
                wyrmguards.push_back(unit);
        }
        return wyrmguards;
    }

    // Shared by the raid, so a bot that joins late does not wait on its own. The window starts at the first check on
    // a pack and a gap without checks ends the pack.
    bool IsWyrmguardHoldRunning(Player* bot)
    {
        uint32 const now = getMSTime();
        std::lock_guard<std::mutex> lock(wyrmguardHoldMutex);
        WyrmguardHold& hold = wyrmguardHolds[bot->GetInstanceId()];
        if (!hold.lastCheckMs || getMSTimeDiff(hold.lastCheckMs, now) >= WYRMGUARD_PACK_GAP_MS)
            hold.startedMs = now;

        hold.lastCheckMs = now;
        return getMSTimeDiff(hold.startedMs, now) < WYRMGUARD_HOLD_LIMIT_MS;
    }

    // Every Wyrmguard is on a tank, spread over as many tanks as there are, and those on different tanks are apart.
    bool AreWyrmguardsSplit(Player* bot, std::vector<Unit*> const& wyrmguards)
    {
        std::set<Unit*> holders;
        for (Unit const* wyrmguard : wyrmguards)
        {
            if (!IsHeldByTank(wyrmguard))
                return false;
            holders.insert(wyrmguard->GetVictim());
        }

        if (holders.size() < std::min<size_t>(PlayerbotAI::GetGroupTankNum(bot), wyrmguards.size()))
            return false;

        for (size_t i = 0; i < wyrmguards.size(); ++i)
        {
            for (size_t j = i + 1; j < wyrmguards.size(); ++j)
            {
                if (wyrmguards[i]->GetVictim() != wyrmguards[j]->GetVictim() &&
                    wyrmguards[i]->GetDistance2d(wyrmguards[j]) < WYRMGUARD_SPLIT_DISTANCE)
                    return false;
            }
        }
        return true;
    }

    Unit* GetOwnWyrmguard(Player* bot, std::vector<Unit*> const& wyrmguards)
    {
        for (Unit* wyrmguard : wyrmguards)
        {
            if (wyrmguard->GetVictim() == bot)
                return wyrmguard;
        }
        return nullptr;
    }

    // For a tank without a Wyrmguard: one on a non-tank, or one from a tank that holds several.
    Unit* GetWyrmguardToTaunt(Player* bot, std::vector<Unit*> const& wyrmguards)
    {
        if (GetOwnWyrmguard(bot, wyrmguards))
            return nullptr;

        for (Unit* wyrmguard : wyrmguards)
        {
            if (!IsHeldByTank(wyrmguard))
                return wyrmguard;
        }

        for (size_t i = 0; i < wyrmguards.size(); ++i)
        {
            for (size_t j = i + 1; j < wyrmguards.size(); ++j)
            {
                if (wyrmguards[i]->GetVictim() == wyrmguards[j]->GetVictim())
                    return wyrmguards[j];
            }
        }
        return nullptr;
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

    // A bot already inside the radius may still step away from the drake.
    bool IsStepTowardsDrake(Player const* bot, std::vector<Unit*> const& drakes, float x, float y)
    {
        for (Unit const* drake : drakes)
        {
            float const distance = drake->GetDistance2d(x, y);
            if (distance < DRAKE_SAFE_DISTANCE && distance < drake->GetDistance2d(bot))
                return true;
        }
        return false;
    }
}
