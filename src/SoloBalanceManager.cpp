// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SoloBalanceManager.h"
#include "SoloBalanceScaling.h"
#include "SoloBalanceSurvival.h"
#include "SoloBalanceMechanics.h"
#include "Creature.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "Map.h"
#include "World.h"
#include "TemporarySummon.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace SoloBalance
{
namespace
{
using MapKey = std::pair<uint32, uint32>;
std::mutex registryMutex;
std::map<MapKey, std::shared_ptr<MapState>> maps;
std::map<ObjectGuid, SurvivalState> savedSurvival;
uint64 Now()
{
    return uint64(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
int RankIndex(Creature* c)
{
    switch (c->GetCreatureInfo()->rank)
    {
    case CREATURE_ELITE_NORMAL: return 0; case CREATURE_ELITE_ELITE: return 1;
    case CREATURE_ELITE_RARE: return 2; case CREATURE_ELITE_RAREELITE: return 3;
    case CREATURE_ELITE_WORLDBOSS: return 4; default: return -1;
    }
}
void Apply(Creature* c, Snapshot& s, Factors const& f, uint32 level, bool statDamage)
{
    bool alive = c->IsAlive();
    double hp = double(c->GetHealth()) / std::max(1u, c->GetMaxHealth());
    double mp = double(c->GetPower(POWER_MANA)) / std::max(1u, c->GetMaxPower(POWER_MANA));
    c->SetLevel(level);
    // Adjust only our contribution to modifier bases. Current aura deltas survive reload.
    UnitMods mods[] = {UNIT_MOD_HEALTH, UNIT_MOD_MANA, UNIT_MOD_ARMOR};
    double previous[] = {s.factors.health, s.factors.mana, s.factors.armor};
    double next[] = {f.health, f.mana, f.armor};
    for (size_t i = 0; i < 3; ++i)
    {
        double old = s.applied ? previous[i] : 1;
        float base = c->GetModifierValue(mods[i], BASE_VALUE);
        c->SetModifierValue(mods[i], BASE_VALUE, float(base + s.statBases[i] * (next[i] - old)));
    }
    c->SetMaxHealth(BoundedUInt(s.health * f.health, 1));
    c->SetMaxPower(POWER_MANA, BoundedUInt(s.mana * f.mana));
    c->SetArmor(int32(BoundedUInt(std::max(0, s.armor) * f.armor)));
    if (statDamage || s.statDamage)
    {
        double damage = statDamage ? f.damage : 1;
        c->SetInt32Value(UNIT_FIELD_ATTACK_POWER, int32(Clamp(s.attackPower[0] * damage, -2147483647.0, 2147483647.0)));
        c->SetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER, int32(Clamp(s.attackPower[1] * damage, -2147483647.0, 2147483647.0)));
        for (size_t attack = 0; attack < 3; ++attack)
        {
            auto type = WeaponAttackType(attack);
            // This core applies AP again as 0.7 + 0.3*AP/templateAP in UpdateDamagePhysical.
            // Compensate that native factor so weapon and AP mutations don't multiply twice.
            double dbAP = attack == RANGED_ATTACK ? c->GetCreatureInfo()->ranged_attack_power : c->GetCreatureInfo()->attack_power;
            double ap = s.attackPower[attack == RANGED_ATTACK ? 1 : 0];
            double correction = 1;
            if (dbAP > 0 && ap >= 0) correction = (0.7 + 0.3 * ap / dbAP) / (0.7 + 0.3 * ap * damage / dbAP);
            for (size_t range = 0; range < 2; ++range)
                c->SetBaseWeaponDamage(type, WeaponDamageRange(range), float(s.weapons[attack][range] * damage * correction));
            c->UpdateDamagePhysical(type);
        }
    }
    c->SetHealth(alive ? BoundedUInt(c->GetMaxHealth() * hp, 1) : 0);
    c->SetPower(POWER_MANA, BoundedUInt(c->GetMaxPower(POWER_MANA) * mp));
    s.factors = f; s.scaledLevel = level; s.applied = true; s.statDamage = statDamage;
}
void Clear(Map* map, MapState& state)
{
    for (auto const& p : state.creatures)
        if (Creature* c = map->GetCreature(p.first)) Restore(c, p.second);
    state.creatures.clear();
    state.pendingRemoval.clear();
}
}
std::shared_ptr<MapState> State(Map* map)
{
    // Registry locks protect value ownership only. No core calls occur under this lock.
    MapKey key(map->GetId(), map->GetInstanceId());
    bool safe = sWorld.getConfig(CONFIG_UINT32_MTCELLS_THREADS) <= 1;
    std::lock_guard<std::mutex> guard(registryMutex);
    auto& state = maps[key];
    if (!state) { state = std::make_shared<MapState>(); state->worldThreadsSafe = safe; }
    return state;
}
void Destroy(Map* map)
{
    MapKey key(map->GetId(), map->GetInstanceId());
    std::lock_guard<std::mutex> guard(registryMutex);
    maps.erase(key);
}
Player* Anchor(Map* map)
{
    Player* result = nullptr;
    for (auto const& ref : map->GetPlayers())
    {
        Player* player = ref.getSource();
        if (!player || !player->IsInWorld()) continue;
        if (result) return nullptr; // Safety disable, NEVER a scaling input.
        result = player; // Includes GMs, dead players, and charmed players.
    }
    return result;
}
Scope Content(Map* map, Settings const& c)
{
    if (!c.enabled || map->IsBattleGround()) return Scope::Disabled;
    if (map->IsRaid()) return c.raids ? Scope::Raid : Scope::Disabled;
    if (map->IsDungeon()) return c.dungeons ? Scope::Dungeon : Scope::Disabled;
    // Continent cell workers may execute unit hooks concurrently within one map.
    // Fail closed for that optional scope rather than lock around live core object access.
    if (sWorld.getConfig(CONFIG_UINT32_MTCELLS_THREADS) > 1 || !State(map)->worldThreadsSafe) return Scope::Disabled;
    return c.world ? Scope::World : Scope::Disabled;
}
bool Eligible(Creature* c, Player* p, Settings const& cfg)
{
    if (!c || !p || !c->IsInWorld() || c->GetMap() != p->GetMap() || Content(c->GetMap(), cfg) == Scope::Disabled)
        return false;
    if (c->IsPet() || c->IsTotem() || c->IsTrigger() || c->GetCreatureType() == CREATURE_TYPE_CRITTER ||
        c->GetOwnerGuid().IsPlayer() || c->GetCharmerOrOwnerPlayerOrPlayerItself() ||
        c->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE_2 |
            UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_PLAYER_CONTROLLED)) return false;
    if (c->IsTemporarySummon())
    {
        auto summon = static_cast<TemporarySummon*>(c);
        if (summon->GetSummonerGuid().IsPlayer()) return false;
        if (Unit* owner = summon->GetSummoner())
            if (owner->GetCharmerOrOwnerPlayerOrPlayerItself()) return false;
    }
    if (c->GetMap()->IsDungeon() == false)
    {
        int rank = RankIndex(c);
        if (rank < 0 || !cfg.worldRanks[size_t(rank)]) return false;
    }
    // GetReactionTo makes GMs neutral, and charm temporarily replaces the player's faction.
    // Use the character's race faction with explicit reputation for those two cases.
    ReputationRank reaction;
    if (p->IsGameMaster() || p->IsCharmed())
    {
        auto faction = c->GetFactionTemplateEntry();
        auto playerFaction = sObjectMgr.GetFactionTemplateEntry(Player::GetFactionForRace(p->GetRace()));
        if (!faction || !playerFaction) return false;
        reaction = faction->IsHostileTo(*playerFaction) ? REP_HOSTILE :
            (faction->IsFriendlyTo(*playerFaction) || playerFaction->IsFriendlyTo(*faction) ? REP_FRIENDLY : REP_NEUTRAL);
        if (auto forced = p->GetReputationMgr().GetForcedRankIfAny(faction)) reaction = *forced;
        else if (auto entry = sObjectMgr.GetFactionEntry(faction->faction))
            if (entry->CanHaveReputation())
            {
                reaction = p->GetReputationMgr().GetRank(entry);
                if (auto reputation = p->GetReputationMgr().GetState(entry))
                    if (reputation->Flags & FACTION_FLAG_AT_WAR) reaction = std::min(reaction, REP_NEUTRAL);
            }
    }
    else reaction = c->GetReactionTo(p);
    // Neutral attackable creatures qualify, but friendly service NPCs never do.
    return reaction < REP_FRIENDLY;
}
Snapshot* Find(Creature* c, MapState& state)
{
    auto it = state.creatures.find(c->GetObjectGuid());
    return it != state.creatures.end() && it->second.entry == c->GetEntry() ? &it->second : nullptr;
}
Snapshot* Ensure(Creature* c, Player* p, MapState& state, Settings const& cfg)
{
    if (!c || !c->IsAlive() || !Eligible(c, p, cfg)) return nullptr;
    BindAnchor(p, state);
    auto it = state.creatures.find(c->GetObjectGuid());
    if (it != state.creatures.end() && it->second.entry != c->GetEntry())
    {
        // UpdateEntry reinitializes stats; a transformed entry starts a fresh lifetime snapshot.
        state.creatures.erase(it); it = state.creatures.end();
    }
    if (it != state.creatures.end() && it->second.applied && it->second.factors.health != 1 &&
        c->GetMaxHealth() == c->GetCreateHealth() &&
        std::abs(double(c->GetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE)) - c->GetCreateHealth()) < 0.5)
    {
        // SelectLevel resets health/mana bases and weapons on same-entry respawn, but not armor/AP.
        // Recover a direct scripted respawn that happened entirely between two discovery scans.
        Snapshot const& old = it->second;
        c->SetModifierValue(UNIT_MOD_ARMOR, BASE_VALUE,
            float(c->GetModifierValue(UNIT_MOD_ARMOR, BASE_VALUE) - old.statBases[2] * (old.factors.armor - 1)));
        c->UpdateArmor();
        if (old.statDamage)
        {
            c->SetInt32Value(UNIT_FIELD_ATTACK_POWER, old.attackPower[0]);
            c->SetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER, old.attackPower[1]);
            c->UpdateDamagePhysical(BASE_ATTACK); c->UpdateDamagePhysical(OFF_ATTACK); c->UpdateDamagePhysical(RANGED_ATTACK);
        }
        state.creatures.erase(it); it = state.creatures.end();
    }
    if (it == state.creatures.end())
    {
        if (state.creatures.size() >= cfg.maxTracked) return nullptr;
        Snapshot s; s.guid = c->GetObjectGuid(); s.entry = c->GetEntry();
        s.nativeLevel = s.scaledLevel = c->GetLevel(); s.health = c->GetMaxHealth();
        s.mana = c->GetMaxPower(POWER_MANA); s.armor = c->GetArmor();
        s.statBases = {{c->GetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE),
            c->GetModifierValue(UNIT_MOD_MANA, BASE_VALUE), c->GetModifierValue(UNIT_MOD_ARMOR, BASE_VALUE)}};
        s.attackPower = {{c->GetInt32Value(UNIT_FIELD_ATTACK_POWER), c->GetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER)}};
        for (size_t a = 0; a < 3; ++a) for (size_t r = 0; r < 2; ++r)
            s.weapons[a][r] = c->GetWeaponDamageRange(WeaponAttackType(a), WeaponDamageRange(r));
        it = state.creatures.emplace(s.guid, s).first;
    }
    Snapshot& s = it->second;
    s.lastSeen = Now();
    if (s.applied && (s.generation == cfg.generation && s.anchorLevel == p->GetLevel())) return &s;
    if (s.applied && c->IsInCombat()) return &s; // Reload/level changes wait for this creature's combat to end.
    s.boss = c->IsWorldBoss() || cfg.bossEntries.count(c->GetEntry()) || cfg.addRules.count(c->GetEntry());
    s.add = c->IsTemporarySummon() && !s.boss;
    double capacity;
    if (c->GetMap()->IsDungeon())
    {
        capacity = std::max(1u, static_cast<DungeonMap*>(c->GetMap())->GetMaxPlayers());
        s.profile = s.boss ? Profile::Boss : (s.add && cfg.addsEnabled ? Profile::Add : Profile::Trash);
    }
    else
    {
        int rank = RankIndex(c);
        if (rank < 0) return nullptr;
        Profile profiles[] = {Profile::Normal, Profile::Elite, Profile::Rare, Profile::RareElite, Profile::Boss};
        capacity = cfg.worldCapacity[size_t(rank)]; s.profile = profiles[rank];
        if (s.boss) s.profile = Profile::Boss;
        else if (s.add && cfg.addsEnabled && rank != 0) s.profile = Profile::Add;
    }
    auto const& policy = c->GetMap()->IsDungeon() ? cfg.instanceLevel : cfg.worldLevel;
    uint32 level = ScaledLevel(s.nativeLevel, p->GetLevel(), policy);
    Apply(c, s, Calculate(capacity, s.profile, s.nativeLevel, level, c->GetMapId(), s.entry, cfg), level, cfg.statDamage);
    s.generation = cfg.generation; s.anchorLevel = p->GetLevel();
    return &s;
}
void Restore(Creature* c, Snapshot const& s)
{
    if (!s.applied || c->GetEntry() != s.entry) return;
    Snapshot copy = s;
    Apply(c, copy, Factors{}, s.nativeLevel, false);
}
void Leave(Map* map, Player* player)
{
    auto state = State(map);
    if (state->anchor != player->GetObjectGuid()) return;
    Clear(map, *state);
    ObjectGuid guid = state->anchor;
    {
        std::lock_guard<std::mutex> guard(registryMutex);
        // This is a one-player server; bound accidental retention on shared test servers.
        if (savedSurvival.size() >= 64 && !savedSurvival.count(guid)) savedSurvival.erase(savedSurvival.begin());
        savedSurvival[guid] = state->survival;
    }
    state->anchor.Clear();
}
void BindAnchor(Player* player, MapState& state)
{
    ObjectGuid guid = player->GetObjectGuid();
    if (state.anchor == guid) return;
    Clear(player->GetMap(), state);
    state.anchor = guid;
    std::lock_guard<std::mutex> guard(registryMutex);
    auto saved = savedSurvival.find(guid);
    state.survival = saved == savedSurvival.end() ? SurvivalState{} : saved->second;
}
void Death(Unit* unit)
{
    Creature* c = unit ? unit->ToCreature() : nullptr;
    if (!c || !c->IsInWorld()) return;
    auto state = State(c->GetMap());
    if (auto s = Find(c, *state))
    {
        Restore(c, *s); // Health remains zero; modifier bases cannot leak into respawn.
        state->creatures.erase(c->GetObjectGuid());
    }
}
Player* CreditedPlayer(Unit* unit, bool includePet)
{
    if (!unit) return nullptr;
    if (unit->IsPlayer()) return unit->ToPlayer();
    if (includePet)
        if (Unit* owner = unit->GetOwner()) return owner->ToPlayer();
    return nullptr;
}
bool Fighting(Player* player, MapState& state)
{
    if (!player->IsAlive() || !player->IsInCombat()) return false;
    for (auto const& p : state.creatures)
    {
        Creature* c = player->GetMap()->GetCreature(p.first);
        if (!c || !c->IsAlive() || !c->IsInCombat()) continue;
        if (CreditedPlayer(c->GetVictim(), true) == player) return true;
        for (Unit* attacker : c->GetAttackers()) if (CreditedPlayer(attacker, true) == player) return true;
    }
    return false;
}
void Update(Map* map, uint32 diff)
{
    auto state = State(map);
    auto cfg = Config();
    state->elapsed = Now();
    state->pendingMs = uint32(std::min<uint64>(uint64(state->pendingMs) + diff, 10000));
    if (state->pendingMs < cfg->interval) return;
    uint32 elapsed = state->pendingMs; state->pendingMs = 0;
    Player* p = Anchor(map);
    if (!p || Content(map, *cfg) == Scope::Disabled)
    {
        Clear(map, *state); return;
    }
    BindAnchor(p, *state);
    // Resolve every retained GUID before expiry; never discard a still-scaled live object.
    for (auto it = state->creatures.begin(); it != state->creatures.end();)
    {
        Creature* c = map->GetCreature(it->first);
        if (!c || c->GetEntry() != it->second.entry) { it = state->creatures.erase(it); continue; }
        if (!c->IsAlive() || !Eligible(c, p, *cfg) ||
            (!c->IsInCombat() && state->elapsed - it->second.lastSeen >= cfg->expiry))
        {
            Restore(c, it->second); it = state->creatures.erase(it); continue;
        }
        ++it;
    }
    double radius = map->IsDungeon() ? cfg->instanceRadius : cfg->radius;
    auto check = [&](Creature* c) { return c->IsWithinDistInMap(p, float(radius)); };
    std::list<Creature*> loaded;
    MaNGOS::CreatureListSearcher<decltype(check)> search(loaded, check);
    Cell::VisitAllObjects(p, search, float(radius), true); // true = do not load grids
    for (Creature* c : loaded) Ensure(c, p, *state, *cfg);
    ProcessAuras(p, *state, *cfg);
    LimitAdds(p, *state, *cfg);
    Sustain(p, *state, *cfg, elapsed);
}
}
