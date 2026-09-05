// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SoloBalanceSurvival.h"
#include "SoloBalanceManager.h"
#include "SoloBalanceScaling.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace SoloBalance
{
namespace
{
uint64 Now()
{
    return uint64(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
void GainHealth(Player* p, uint32 amount)
{
    if (p->IsAlive()) p->ModifyHealth(int32(std::min(amount, p->GetMaxHealth() - p->GetHealth())));
}
}
void Damage(Unit* attacker, Unit* victim, uint32& damage)
{
    if (!attacker || !victim || attacker == victim || !damage || !attacker->IsInWorld() || !victim->IsInWorld() || !victim->IsAlive() ||
        attacker->GetMap() != victim->GetMap()) return;
    auto cfg = Config();
    if (Content(victim->GetMap(), *cfg) == Scope::Disabled) return;
    Player* anchor = Anchor(victim->GetMap());
    if (!anchor) return;
    auto state = State(victim->GetMap());
    Creature* source = attacker->ToCreature();
    Player* receiver = CreditedPlayer(victim, true);
    if (source && receiver == anchor)
    {
        Snapshot* s = Ensure(source, anchor, *state, *cfg);
        if (!s) return;
        // UNIVERSAL path is the sole damage scaler in default mode. Do not also mutate weapons.
        if (!s->statDamage) damage = BoundedUInt(damage * s->factors.damage);
        if (victim == anchor)
        {
            damage = std::min(damage, BoundedUInt(anchor->GetMaxHealth() * cfg->maxHit, 1));
            uint64 now = Now();
            if (cfg->emergency && damage >= anchor->GetHealth() && now >= state->survival.emergencyReady)
            {
                damage = anchor->GetHealth() - 1;
                state->survival.emergencyReady = now + uint64(cfg->emergencyCooldown) * 1000;
            }
        }
        return;
    }
    Player* dealer = CreditedPlayer(attacker, cfg->petLeech);
    Creature* target = victim->ToCreature();
    if (!cfg->leech || dealer != anchor || !dealer->IsAlive() || !target || target->IsInEvadeMode()) return;
    if (!Ensure(target, anchor, *state, *cfg)) return;
    // Cap credit at damage that can reduce health; no overkill/invincibility farming.
    uint32 floor = target->GetInvincibilityHpThreshold();
    uint32 effective = std::min(damage, target->GetHealth() > floor ? target->GetHealth() - floor : 0);
    double wanted = std::floor(effective * cfg->leechPct);
    uint64 now = Now(), bucket = now / 100;
    auto& budget = state->survival;
    double used = 0;
    for (size_t i = 0; i < budget.healed.size(); ++i)
    {
        // Retain the partial oldest bucket: rolling cap is conservative by at most 100 ms.
        if (bucket >= budget.bucketTime[i] && bucket - budget.bucketTime[i] <= 10) used += budget.healed[i];
    }
    size_t slot = size_t(bucket % budget.healed.size());
    if (budget.bucketTime[slot] != bucket) { budget.bucketTime[slot] = bucket; budget.healed[slot] = 0; }
    double available = std::max(0.0, dealer->GetMaxHealth() * cfg->leechCap - used);
    uint32 amount = std::min(BoundedUInt(std::min(wanted, available)), dealer->GetMaxHealth() - dealer->GetHealth());
    budget.healed[slot] += amount;
    GainHealth(dealer, amount); // ModifyHealth does not invoke OnDamage or OnHeal.
}
void Heal(Unit* healer, Unit* target, uint32& heal)
{
    if (!healer || !target || !target->IsAlive() || !target->IsInWorld() || !healer->IsInWorld() || healer->GetMap() != target->GetMap() ||
        healer->GetCharmerOrOwnerPlayerOrPlayerItself()) return;
    Creature* c = target->ToCreature();
    if (!c) return;
    auto cfg = Config();
    Player* p = Anchor(c->GetMap());
    if (!p || Content(c->GetMap(), *cfg) == Scope::Disabled) return;
    auto state = State(c->GetMap());
    if (Snapshot* s = Ensure(c, p, *state, *cfg)) heal = BoundedUInt(heal * s->factors.health);
}
void Sustain(Player* p, MapState& state, Settings const& cfg, uint32 diff)
{
    if (!Fighting(p, state))
    {
        state.survival.manaCarry = state.survival.healthCarry = 0;
        return;
    }
    // Don't award a large catch-up burst after a stalled update.
    double fraction = std::min(diff, 5000u) / 5000.0;
    auto& s = state.survival;
    s.manaCarry += p->GetMaxPower(POWER_MANA) * cfg.manaPer5 * fraction;
    s.healthCarry += p->GetMaxHealth() * cfg.healthPer5 * fraction;
    uint32 mana = BoundedUInt(s.manaCarry), health = BoundedUInt(s.healthCarry);
    s.manaCarry -= mana; s.healthCarry -= health;
    p->ModifyPower(POWER_MANA, int32(std::min(mana, p->GetMaxPower(POWER_MANA) - p->GetPower(POWER_MANA))));
    GainHealth(p, health);
}
}
