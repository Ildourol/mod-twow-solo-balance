// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SoloBalanceManager.h"
#include "SoloBalanceScaling.h"
#include "ScriptObjects.h"
#include "WorldSession.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "World.h"
#include <sstream>

namespace
{
void ShowHelp(ChatHandler* h)
{
    h->SendSysMessage("|cff00ff00=== SoloBalance In-Game Commands ===|r");
    h->SendSysMessage("  |cffffd100.solo info|r       - Display current SoloBalance configuration and map status");
    h->SendSysMessage("  |cffffd100.solo stats|r      - Display active scaling stats, sustain, and survival info");
    h->SendSysMessage("  |cffffd100.solo creature|r   - Display scaling multipliers and health of targeted creature");
    h->SendSysMessage("  |cffffd100.solo rescale|r    - Force immediate rescale of target (or entire instance/map)");
    h->SendSysMessage("  |cffffd100.solo reload|r     - Reload module configuration from file");
}

void Info(ChatHandler* h, Player* p)
{
    auto c = SoloBalance::Config();
    auto state = SoloBalance::State(p->GetMap());
    auto anchor = SoloBalance::Anchor(p->GetMap());
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r Status: |cffffd100%s|r | Preset: |cffffd100%s|r | Difficulty: |cffffd100%.2f|r | Damage Mode: |cffffd100%s|r (Gen %u)",
        c->enabled ? "Enabled" : "Disabled", c->preset.c_str(), c->difficulty, c->statDamage ? "Stats" : "Universal", c->generation);
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r Anchor: |cffffd100%s|r (Level %u) | Map: %u (Inst %u) | Scope: |cffffd100%s|r | Tracked: %u/%u",
        anchor ? anchor->GetName() : "none", anchor ? anchor->GetLevel() : 0,
        p->GetMapId(), p->GetInstanceId(), SoloBalance::Name(SoloBalance::Content(p->GetMap(), *c)),
        unsigned(state->creatures.size()), c->maxTracked);
    auto const& policy = p->GetMap()->IsDungeon() ? c->instanceLevel : c->worldLevel;
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r Scopes: Dungeons [%s] | Raids [%s] | World [%s] | Level Policy: %s [-%d, +%d] | Scan: %u ms",
        c->dungeons ? "ON" : "OFF", c->raids ? "ON" : "OFF", c->world ? "ON" : "OFF",
        SoloBalance::Name(policy.mode), policy.below, policy.above, c->interval);
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r Sustain: Leech %.1f%% (Cap %.1f%%/s, Pets: %s) | Mana: %.1f%%/5s | Health: %.1f%%/5s",
        c->leechPct * 100.0, c->leechCap * 100.0, c->petLeech ? "Yes" : "No", c->manaPer5 * 100.0, c->healthPer5 * 100.0);
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r Survival: CC Cap %u ms | Charm Cap %u ms | Max Hit: %.0f%% MaxHP | Emergency: %s (%u s CD)",
        c->ccMax, c->charmMax, c->maxHit * 100.0, c->emergency ? "Enabled" : "Disabled", c->emergencyCooldown);
    if (p->GetMap()->IsDungeon())
    {
        uint32 maxPlayers = dynamic_cast<DungeonMap*>(p->GetMap()) ? dynamic_cast<DungeonMap*>(p->GetMap())->GetMaxPlayers() : 5;
        SoloBalance::Factors trash = SoloBalance::Calculate(maxPlayers, SoloBalance::Profile::Trash, 60, 60, p->GetMapId(), 0, *c);
        SoloBalance::Factors boss = SoloBalance::Calculate(maxPlayers, SoloBalance::Profile::Boss, 60, 60, p->GetMapId(), 0, *c);
        h->PSendSysMessage("|cff00ff00[SoloBalance]|r Current Instance: %u players | Trash HP: %.1f%% DMG: %.1f%% | Boss HP: %.1f%% DMG: %.1f%%",
            maxPlayers, trash.health * 100.0, trash.damage * 100.0, boss.health * 100.0, boss.damage * 100.0);
    }
}

void Stats(ChatHandler* h, Player* p)
{
    Info(h, p);
}

void CreatureInfo(ChatHandler* h, Player* p)
{
    auto c = p->GetSelectedCreature();
    if (!c) { h->SendSysMessage("Select a creature first."); return; }
    auto config = SoloBalance::Config();
    auto state = SoloBalance::State(p->GetMap());
    auto s = SoloBalance::Find(c, *state);
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r Creature: |cffffd100%s|r (Entry %u, GUID: %s) | Eligible: %s",
        c->GetName(), c->GetEntry(), c->GetObjectGuid().GetString().c_str(),
        (SoloBalance::Anchor(p->GetMap()) == p && SoloBalance::Eligible(c, p, *config)) ? "YES" : "NO");
    if (!s)
    {
        h->SendSysMessage("No live snapshot active for this creature (may not be scaled or not eligible).");
        return;
    }
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r Profile: |cffffd100%s|r (Boss: %s, Add: %s) | Level: %u (Native: %u)",
        SoloBalance::Name(s->profile), s->boss ? "Yes" : "No", s->add ? "Yes" : "No", s->scaledLevel, s->nativeLevel);
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r HP: %u / %u (Native: %u) | Mana: %u / %u (Native: %u)",
        c->GetHealth(), c->GetMaxHealth(), s->health, c->GetPower(POWER_MANA), c->GetMaxPower(POWER_MANA), s->mana);
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r Multipliers: HP: |cffffd100%.4f|r | Damage: |cffffd100%.4f|r | Armor: |cffffd100%.4f|r | CC: |cffffd100%.4f|r",
        s->factors.health, s->factors.damage, s->factors.armor, s->factors.cc);
    h->PSendSysMessage("|cff00ff00[SoloBalance]|r Damage Mode: %s | Combat: %s | Config Gen: %u (Current: %u)",
        s->statDamage ? "Stats (Weapons & AP scaled)" : "Universal", c->IsInCombat() ? "In Combat" : "Out of Combat",
        s->generation, config->generation);
}

class SoloCommands final : public AllCommandScript
{
public:
    SoloCommands() : AllCommandScript("twow_solo_commands") {}
    bool CanExecuteCommand(ChatHandler* handler, char const* command, char const* args) override
    {
        if (std::string(command) != "solo") return true;
        auto session = handler->GetSession();
        std::istringstream input(args ? args : "");
        std::string sub;
        input >> sub;
        if (!session)
        {
            if (sub == "reload")
            {
                std::string error;
                if (SoloBalance::Reload(error)) handler->SendSysMessage("[SoloBalance] Configuration reloaded.");
                else handler->PSendSysMessage("[SoloBalance] Reload rejected: %s", error.c_str());
                return false;
            }
            handler->SendSysMessage("SoloBalance commands require an in-game player session (except .solo reload).");
            return false;
        }

        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld()) return false;

        if (sub.empty() || sub == "help")
        {
            ShowHelp(handler);
            return false;
        }
        else if (sub == "info")
        {
            Info(handler, player);
            return false;
        }
        else if (sub == "stats")
        {
            Stats(handler, player);
            return false;
        }
        else if (sub == "creature")
        {
            CreatureInfo(handler, player);
            return false;
        }
        else if (sub == "reload")
        {
            if (session->GetSecurity() < SEC_DEVELOPER)
            {
                handler->SendSysMessage("Reload requires security >= SEC_DEVELOPER (3).");
                return false;
            }
            std::string error;
            if (SoloBalance::Reload(error))
            {
                handler->SendSysMessage("|cff00ff00[SoloBalance]|r Configuration reloaded successfully.");
            }
            else
            {
                handler->PSendSysMessage("|cffff0000[SoloBalance]|r Reload rejected; previous settings retained: %s", error.c_str());
            }
            return false;
        }
        else if (sub == "rescale")
        {
            if (session->GetSecurity() < SEC_DEVELOPER)
            {
                handler->SendSysMessage("Rescale requires security >= SEC_DEVELOPER (3).");
                return false;
            }
            auto cfg = SoloBalance::Config();
            auto state = SoloBalance::State(player->GetMap());
            Creature* c = player->GetSelectedCreature();
            if (c)
            {
                if (SoloBalance::Eligible(c, player, *cfg))
                {
                    if (auto s = SoloBalance::Find(c, *state))
                        s->applied = false;
                    if (SoloBalance::Ensure(c, player, *state, *cfg))
                        handler->PSendSysMessage("|cff00ff00[SoloBalance]|r Rescaled %s (entry %u).", c->GetName(), c->GetEntry());
                    else
                        handler->SendSysMessage("Failed to rescale selected creature.");
                }
                else handler->SendSysMessage("Selected creature is not eligible for SoloBalance scaling.");
            }
            else
            {
                if (player->GetMap()->IsDungeon())
                {
                    SoloBalance::ScaleInstance(player->GetMap(), player);
                    handler->PSendSysMessage("|cff00ff00[SoloBalance]|r Rescaled %u tracked creatures in instance %u.",
                        unsigned(state->creatures.size()), player->GetMapId());
                }
                else
                {
                    uint32 count = 0;
                    for (auto& pair : state->creatures)
                    {
                        if (Creature* creature = player->GetMap()->GetCreature(pair.first))
                        {
                            pair.second.applied = false;
                            if (SoloBalance::Ensure(creature, player, *state, *cfg))
                                ++count;
                        }
                    }
                    handler->PSendSysMessage("|cff00ff00[SoloBalance]|r Rescaled %u tracked creatures on map %u.", count, player->GetMapId());
                }
            }
            return false;
        }
        else
        {
            handler->SendSysMessage("Unknown subcommand. Type '.solo help' for a list of available commands.");
            return false;
        }
    }
};
}
void RegisterSoloBalanceCommands() { new SoloCommands(); }
