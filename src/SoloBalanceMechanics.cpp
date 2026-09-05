// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SoloBalanceMechanics.h"
#include "SoloBalanceManager.h"
#include "SoloBalanceScaling.h"
#include "Player.h"
#include "Creature.h"
#include "TemporarySummon.h"
#include "Map.h"
#include "SpellAuras.h"
#include <algorithm>
#include <limits>

namespace SoloBalance
{
namespace
{
void Cap(SpellAuraHolder* holder, Snapshot const& s, Settings const& cfg)
{
    uint32 cap = std::numeric_limits<uint32>::max();
    uint32 mechanics[] = {MECHANIC_CHARM, MECHANIC_DISORIENTED, MECHANIC_FEAR, MECHANIC_ROOT,
        MECHANIC_SILENCE, MECHANIC_SLEEP, MECHANIC_STUN, MECHANIC_FREEZE, MECHANIC_POLYMORPH,
        MECHANIC_BANISH, MECHANIC_HORROR, MECHANIC_SAPPED};
    bool cc = false, charm = false;
    for (uint32 mechanic : mechanics)
    {
        if (!holder->HasMechanic(mechanic)) continue;
        cc = true; charm |= mechanic == MECHANIC_CHARM;
    }
    for (auto const& rule : cfg.mechanicCaps)
        if (holder->HasMechanic(rule.first)) { cc = true; cap = std::min(cap, rule.second); }
    // Some Vanilla spells omit mechanic metadata; inspect their actual aura type too.
    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        Aura* a = holder->GetAuraByEffectIndex(SpellEffectIndex(i));
        if (!a || a->IsPositive()) continue;
        switch (a->GetModifier()->m_auraname)
        {
        case SPELL_AURA_MOD_POSSESS: case SPELL_AURA_MOD_CHARM: case SPELL_AURA_MOD_POSSESS_PET:
            charm = true; cc = true; break;
        case SPELL_AURA_MOD_STUN: case SPELL_AURA_MOD_FEAR: case SPELL_AURA_MOD_CONFUSE:
        case SPELL_AURA_MOD_ROOT: case SPELL_AURA_MOD_SILENCE: cc = true; break;
        default: break;
        }
    }
    if (cc) cap = std::min(cap, BoundedUInt(cfg.ccMax * s.factors.cc, 100));
    if (charm) cap = std::min(cap, cfg.charmMax);
    auto durOverride = cfg.durations.find(holder->GetId());
    if (durOverride != cfg.durations.end()) cap = std::min(cap, durOverride->second);
    if (cap == std::numeric_limits<uint32>::max()) return;
    int32 limit = int32(cap);
    if (holder->GetAuraMaxDuration() < 0 || holder->GetAuraMaxDuration() > limit) holder->SetAuraMaxDuration(limit);
    if (holder->GetAuraDuration() < 0 || holder->GetAuraDuration() > limit) holder->SetAuraDuration(limit);
}
}
void AuraApplied(Unit* target, Aura* aura)
{
    if (!target || !target->IsPlayer() || !target->IsInWorld() || !aura || aura->IsPositive()) return;
    auto cfg = Config();
    if (!cfg->cc || Content(target->GetMap(), *cfg) == Scope::Disabled || Anchor(target->GetMap()) != target) return;
    Creature* caster = aura->GetCaster() ? aura->GetCaster()->ToCreature() : nullptr;
    if (!caster || caster == target) return;
    auto state = State(target->GetMap());
    if (state->processingAuras) return;
    Snapshot* s = Ensure(caster, target->ToPlayer(), *state, *cfg);
    if (!s) return;
    Cap(aura->GetHolder(), *s, *cfg);
    if (cfg->removeAuras.count(aura->GetId())) state->pendingRemoval.emplace(aura->GetId(), aura->GetCasterGuid());
}
void ProcessAuras(Player* p, MapState& state, Settings const& cfg)
{
    if (!cfg.cc || state.processingAuras) return;
    state.processingAuras = true;
    // Store identifiers, not Aura pointers, across removal/reapplication calls.
    std::vector<std::pair<uint32, ObjectGuid>> holders;
    for (auto const& pair : p->GetSpellAuraHolderMap())
        if (!pair.second->IsPositive() && !pair.second->IsDeleted()) holders.emplace_back(pair.first, pair.second->GetCasterGuid());
    for (auto const& key : holders)
    {
        auto holder = p->GetSpellAuraHolder(key.first, key.second);
        if (!holder || holder->IsInUse() || holder->IsDeleted() || holder->IsPositive()) continue;
        // A removal queued by the verified apply hook remains valid if its caster just died.
        if (state.pendingRemoval.erase(key)) { p->RemoveSpellAuraHolder(holder); continue; }
        auto caster = holder->GetCaster();
        Creature* c = caster ? caster->ToCreature() : nullptr;
        if (!c || !Eligible(c, p, cfg)) continue;
        Snapshot* s = Find(c, state);
        if (!s) continue;
        if (cfg.removeAuras.count(key.first)) { p->RemoveSpellAuraHolder(holder); continue; }
        Cap(holder, *s, cfg);
        auto limit = cfg.stacks.find(key.first);
        if (limit != cfg.stacks.end() && holder->GetStackAmount() > limit->second) holder->SetStackAmount(limit->second);
    }
    // Drop queue entries whose exact holder disappeared; bound queue by live player auras.
    for (auto it = state.pendingRemoval.begin(); it != state.pendingRemoval.end();)
        if (!p->GetSpellAuraHolder(it->first, it->second)) it = state.pendingRemoval.erase(it); else ++it;
    state.processingAuras = false;
}
void LimitAdds(Player* p, MapState& state, Settings const& cfg)
{
    if (!cfg.addsEnabled || cfg.addRules.empty()) return;
    // Exact summoner GUID + configured add entry, never a global summon count.
    std::map<std::pair<ObjectGuid, uint32>, std::vector<ObjectGuid>> groups;
    for (auto const& pair : state.creatures)
    {
        Creature* c = p->GetMap()->GetCreature(pair.first);
        if (!c || !c->IsAlive() || !c->IsTemporarySummon() || !Eligible(c, p, cfg)) continue;
        auto summon = static_cast<TemporarySummon*>(c);
        Creature* boss = p->GetMap()->GetCreature(summon->GetSummonerGuid());
        if (!boss || !boss->IsAlive() || !boss->IsInCombat() || !Find(boss, state) || !Eligible(boss, p, cfg)) continue;
        if (CreditedPlayer(boss->GetVictim(), true) != p) continue;
        if (cfg.addRules.count(boss->GetEntry())) groups[{boss->GetObjectGuid(), c->GetEntry()}].push_back(c->GetObjectGuid());
    }
    for (auto& group : groups)
    {
        Creature* boss = p->GetMap()->GetCreature(group.first.first);
        if (!boss) continue;
        auto rules = cfg.addRules.find(boss->GetEntry());
        if (rules == cfg.addRules.end()) continue;
        for (auto const& rule : rules->second)
        {
            if (rule.add != group.first.second) continue;
            auto& adds = group.second;
            std::sort(adds.begin(), adds.end());
            for (size_t i = rule.maximum; i < adds.size(); ++i)
            {
                Creature* c = p->GetMap()->GetCreature(adds[i]);
                if (!c || !c->IsTemporarySummon()) continue;
                if (auto s = Find(c, state)) Restore(c, *s);
                state.creatures.erase(adds[i]);
                static_cast<TemporarySummon*>(c)->UnSummon();
            }
        }
    }
}
}
