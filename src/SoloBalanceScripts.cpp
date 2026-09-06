// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SoloBalanceManager.h"
#include "SoloBalanceSurvival.h"
#include "SoloBalanceMechanics.h"
#include "SoloBalanceScaling.h"
#include "ScriptObjects.h"
#include "Creature.h"
#include "Player.h"
#include "Map.h"
#include "Formulas.h"

namespace
{
class SoloWorld final : public WorldScript
{
public:
    SoloWorld() : WorldScript("twow_solo_world", {WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_AFTER_CONFIG_LOAD}) {}
    void OnStartup() override { std::string error; SoloBalance::Reload(error); }
    void OnAfterConfigLoad(bool) override { std::string error; SoloBalance::Reload(error); }
};
class SoloMap final : public AllMapScript
{
public:
    SoloMap() : AllMapScript("twow_solo_map") {}
    void OnCreateMap(Map* map) override { SoloBalance::State(map); }
    void OnPlayerEnterAll(Map* map, Player* player) override { SoloBalance::OnPlayerEnter(map, player); }
    void OnMapUpdate(Map* map, uint32 diff) override { SoloBalance::Update(map, diff); }
    void OnPlayerLeaveAll(Map* map, Player* player) override { SoloBalance::Leave(map, player); }
    void OnDestroyMap(Map* map) override { SoloBalance::Destroy(map); }
};
class SoloUnit final : public UnitScript
{
public:
    SoloUnit() : UnitScript("twow_solo_unit", {UNITHOOK_ON_DAMAGE, UNITHOOK_ON_HEAL,
        UNITHOOK_ON_AURA_APPLY, UNITHOOK_ON_UNIT_ENTER_COMBAT, UNITHOOK_ON_UNIT_DEATH,
        UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN}) {}
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override { SoloBalance::Damage(attacker, victim, damage); }
    void OnHeal(Unit* healer, Unit* target, uint32& heal) override { SoloBalance::Heal(healer, target, heal); }
    void OnAuraApply(Unit* unit, Aura* aura) override { SoloBalance::AuraApplied(unit, aura); }
    void OnUnitDeath(Unit* unit, Unit*) override { SoloBalance::Death(unit); }
    void OnUnitEnterCombat(Unit* unit, Unit*) override
    {
        Creature* c = unit ? unit->ToCreature() : nullptr;
        if (!c || !c->IsInWorld()) return;
        auto config = SoloBalance::Config();
        if (auto p = SoloBalance::Anchor(c->GetMap()))
        {
            auto state = SoloBalance::State(c->GetMap());
            SoloBalance::Ensure(c, p, *state, *config);
        }
    }
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellEntry const* spell) override
    {
        if (!target || !attacker || !spell || damage <= 0 || !target->IsInWorld() || !attacker->IsInWorld() || target->GetMap() != attacker->GetMap()) return;
        Creature* c = attacker->ToCreature();
        if (!c) return;
        auto cfg = SoloBalance::Config();
        Player* p = SoloBalance::Anchor(c->GetMap());
        if (!p || SoloBalance::CreditedPlayer(target, true) != p) return;
        auto state = SoloBalance::State(c->GetMap());
        auto s = SoloBalance::Ensure(c, p, *state, *cfg);
        if (!s || !s->statDamage) return;
        // Weapon spells have already used the scaled weapon fields, including Holy weapon effects.
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            switch (spell->Effect[i])
            {
            case SPELL_EFFECT_WEAPON_DAMAGE: case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
            case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE: case SPELL_EFFECT_NORMALIZED_WEAPON_DMG: return;
            default: break;
            }
        damage = int32(SoloBalance::BoundedUInt(damage * s->factors.damage));
    }
};
class SoloPlayer final : public PlayerScript
{
public:
    SoloPlayer() : PlayerScript("twow_solo_rewards", {PLAYERHOOK_ON_GIVE_EXP, PLAYERHOOK_ON_MONEY_CHANGED, PLAYERHOOK_ON_LOGIN}) {}
    void OnLogin(Player* player) override
    {
        SoloBalance::EchoLoginStats(player);
    }
    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
    {
        Creature* c = victim ? victim->ToCreature() : nullptr;
        auto cfg = SoloBalance::Config();
        if (!c || !player->IsInWorld() || c->GetMap() != player->GetMap() ||
            SoloBalance::Content(player->GetMap(), *cfg) == SoloBalance::Scope::Disabled ||
            SoloBalance::Anchor(player->GetMap()) != player) return;
        auto state = SoloBalance::State(player->GetMap());
        auto s = SoloBalance::Find(c, *state);
        if (s && c->GetLevel() != s->nativeLevel)
        {
            // Gain is a synchronous inline function with no script dispatch in this checkout.
            // SetLevel only updates a creature's level field. Restore it before leaving this hook.
            // This preserves native-level XP while retaining any caller's fixed reward factor.
            uint32 actualLevel = c->GetLevel();
            uint32 scaledGain = MaNGOS::XP::Gain(player, c);
            c->SetLevel(s->nativeLevel);
            uint32 nativeGain = MaNGOS::XP::Gain(player, c);
            c->SetLevel(actualLevel);
            if (scaledGain) amount = SoloBalance::BoundedUInt(double(amount) * nativeGain / scaledGain + 0.5);
        }
        if (cfg->xpModifier != 1.0)
            amount = SoloBalance::BoundedUInt(double(amount) * cfg->xpModifier);
    }
    void OnMoneyChanged(Player* player, int32& amount) override
    {
        if (amount <= 0) return;
        auto cfg = SoloBalance::Config();
        if (cfg->moneyModifier == 1.0) return;
        if (!player || !player->IsInWorld() ||
            SoloBalance::Content(player->GetMap(), *cfg) == SoloBalance::Scope::Disabled ||
            SoloBalance::Anchor(player->GetMap()) != player) return;
        auto state = SoloBalance::State(player->GetMap());
        if (!SoloBalance::Fighting(player, *state)) return;
        amount = int32(SoloBalance::BoundedUInt(double(amount) * cfg->moneyModifier));
    }
};
}
void RegisterSoloBalanceScripts()
{
    new SoloWorld(); new SoloMap(); new SoloUnit(); new SoloPlayer();
}
