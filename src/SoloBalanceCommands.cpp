// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SoloBalanceManager.h"
#include "ScriptObjects.h"
#include "WorldSession.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "World.h"
#include <sstream>

namespace
{
void Info(ChatHandler* h, Player* p)
{
    auto c = SoloBalance::Config();
    auto state = SoloBalance::State(p->GetMap());
    auto anchor = SoloBalance::Anchor(p->GetMap());
    h->PSendSysMessage("SoloBalance: enabled=%u preset=%s difficulty=%.2f damage=%s config-generation=%u",
        unsigned(c->enabled), c->preset.c_str(), c->difficulty, c->statDamage ? "stats" : "universal", c->generation);
    h->PSendSysMessage("Anchor: %s GUID=%s level=%u; map=%u instance=%u scope=%s; tracked=%u/%u",
        anchor ? anchor->GetName() : "none (empty or multiple players)",
        anchor ? anchor->GetObjectGuid().GetString().c_str() : "none", anchor ? anchor->GetLevel() : 0,
        p->GetMapId(), p->GetInstanceId(), SoloBalance::Name(SoloBalance::Content(p->GetMap(), *c)),
        unsigned(state->creatures.size()), c->maxTracked);
    auto const& policy = p->GetMap()->IsDungeon() ? c->instanceLevel : c->worldLevel;
    h->PSendSysMessage("Scopes: dungeon=%u raid=%u world=%u; level=%s [-%d,+%d]; scan=%u ms",
        unsigned(c->dungeons), unsigned(c->raids), unsigned(c->world), SoloBalance::Name(policy.mode), policy.below, policy.above, c->interval);
    h->PSendSysMessage("World ranks: normal=%u elite=%u rare=%u rare-elite=%u world-boss=%u",
        unsigned(c->worldRanks[0]), unsigned(c->worldRanks[1]), unsigned(c->worldRanks[2]), unsigned(c->worldRanks[3]), unsigned(c->worldRanks[4]));
    if (c->world && (sWorld.getConfig(CONFIG_UINT32_MTCELLS_THREADS) > 1 || !state->worldThreadsSafe))
        h->SendSysMessage("World scope is safely disabled: MapUpdate.Continents.MTCells.Threads must be 0 or 1 for serialized unit hooks.");
    h->PSendSysMessage("Sustain: leech=%u fraction=%.3f cap/sec=%.3f pets=%u mana/5s=%.3f health/5s=%.3f",
        unsigned(c->leech), c->leechPct, c->leechCap, unsigned(c->petLeech), c->manaPer5, c->healthPer5);
    h->PSendSysMessage("Survival: CC=%u cap=%u ms charm=%u ms hit-cap=%.2f emergency=%u cooldown=%u sec",
        unsigned(c->cc), c->ccMax, c->charmMax, c->maxHit, unsigned(c->emergency), c->emergencyCooldown);
    size_t addRules = 0; for (auto const& pair : c->addRules) addRules += pair.second.size();
    h->PSendSysMessage("Rules: durations=%u removals=%u stacks=%u mechanic-caps=%u maps=%u creatures=%u boss-entries=%u boss/add-pairs=%u",
        unsigned(c->durations.size()), unsigned(c->removeAuras.size()), unsigned(c->stacks.size()), unsigned(c->mechanicCaps.size()),
        unsigned(c->mapOverrides.size()), unsigned(c->creatureOverrides.size()), unsigned(c->bossEntries.size()), unsigned(addRules));
}
void CreatureInfo(ChatHandler* h, Player* p)
{
    auto c = p->GetSelectedCreature();
    if (!c) { h->SendSysMessage("Select a creature first."); return; }
    auto config = SoloBalance::Config();
    auto state = SoloBalance::State(p->GetMap());
    auto s = SoloBalance::Find(c, *state);
    h->PSendSysMessage("Creature entry=%u GUID=%s eligible=%u", c->GetEntry(), c->GetObjectGuid().GetString().c_str(),
        unsigned(SoloBalance::Anchor(p->GetMap()) == p && SoloBalance::Eligible(c, p, *config)));
    if (!s) { h->SendSysMessage("No live snapshot. Wait for discovery, or check scope, rank, flags and scan radius."); return; }
    h->PSendSysMessage("Profile=%s boss=%u add=%u native/scaled level=%u/%u; HP=%u/%u (native max=%u)",
        SoloBalance::Name(s->profile), unsigned(s->boss), unsigned(s->add), s->nativeLevel, s->scaledLevel, c->GetHealth(), c->GetMaxHealth(), s->health);
    h->PSendSysMessage("Multipliers: HP=%.5f mana=%.5f armor=%.5f damage=%.5f CC=%.5f; applied-damage-mode=%s",
        s->factors.health, s->factors.mana, s->factors.armor, s->factors.damage, s->factors.cc, s->statDamage ? "stats" : "universal");
    h->PSendSysMessage("Overrides: map=%u creature=%u; applied/config generation=%u/%u; combat=%u",
        unsigned(config->mapOverrides.count(p->GetMapId())), unsigned(config->creatureOverrides.count(c->GetEntry())),
        s->generation, config->generation, unsigned(c->IsInCombat()));
}
class SoloCommands final : public AllCommandScript
{
public:
    SoloCommands() : AllCommandScript("twow_solo_commands") {}
    bool CanExecuteCommand(ChatHandler* handler, char const* command, char const* args) override
    {
        if (std::string(command) != "solo") return true;
        // ChatCommand stores only ChatHandler member-function pointers in this revision.
        // The verified interceptor is the module-only extension point for new handlers.
        auto session = handler->GetSession();
        std::istringstream input(args ? args : ""); std::string sub, extra;
        input >> sub >> extra;
        if (!session)
        {
            if (sub == "reload")
            {
                std::string error;
                if (SoloBalance::Reload(error)) handler->SendSysMessage("SoloBalance config reloaded.");
                else handler->PSendSysMessage("SoloBalance reload rejected: %s", error.c_str());
                return false;
            }
            handler->SendSysMessage("SoloBalance commands require an in-game player session (except .solo reload).");
            return false;
        }
        if (session->GetSecurity() < SEC_DEVELOPER)
        {
            handler->SendSysMessage("SoloBalance requires an in-game account with security >= SEC_DEVELOPER (3).");
            return false;
        }
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld()) return false;
        if (!extra.empty()) { handler->SendSysMessage("Usage: .solo info | .solo creature | .solo reload | .solo rescale"); return false; }
        if (sub == "info") Info(handler, player);
        else if (sub == "creature") CreatureInfo(handler, player);
        else if (sub == "reload")
        {
            if (session->GetSecurity() < SEC_ADMINISTRATOR) handler->SendSysMessage("Reload requires SEC_ADMINISTRATOR (4).");
            else
            {
                std::string error;
                if (SoloBalance::Reload(error)) handler->SendSysMessage("SoloBalance config reloaded. Combat stat changes wait for combat to end; disabling restores on the next scan.");
                else handler->PSendSysMessage("SoloBalance reload rejected; previous settings retained: %s", error.c_str());
            }
        }
        else if (sub == "rescale")
        {
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
                        handler->PSendSysMessage("Rescaled %s (entry %u).", c->GetName(), c->GetEntry());
                    else
                        handler->SendSysMessage("Failed to rescale selected creature.");
                }
                else handler->SendSysMessage("Selected creature is not eligible for SoloBalance scaling.");
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
                handler->PSendSysMessage("Rescaled %u tracked creatures on map %u.", count, player->GetMapId());
            }
        }
        else handler->SendSysMessage("Usage: .solo info | .solo creature | .solo reload | .solo rescale");
        return false; // This command was consumed, including denied/invalid invocations.
    }
};
}
void RegisterSoloBalanceCommands() { new SoloCommands(); }
