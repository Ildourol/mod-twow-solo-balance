// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SoloBalanceScaling.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace SoloBalance
{
char const* Name(Profile p)
{
    switch (p) {
    case Profile::Normal: return "normal"; case Profile::Elite: return "elite";
    case Profile::Rare: return "rare"; case Profile::RareElite: return "rare-elite";
    case Profile::Boss: return "boss"; case Profile::Add: return "add";
    default: return "trash"; }
}
char const* Name(Scope s)
{
    switch (s) { case Scope::World: return "world"; case Scope::Dungeon: return "dungeon";
    case Scope::Raid: return "raid"; default: return "disabled"; }
}
char const* Name(LevelMode m)
{
    switch (m) { case LevelMode::Native: return "native"; case LevelMode::Follow: return "follow";
    default: return "downscale"; }
}
double Clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
uint32 BoundedUInt(double v, uint32 minimum)
{
    // Core health/damage APIs convert to signed int32 internally.
    return uint32(Clamp(std::isfinite(v) ? v : minimum, minimum, 2147483647.0));
}
double OnePlayerCurve(double capacity, Settings const& c)
{
    if (capacity <= 1) return 1;
    // Normalize the sigmoid at zero and at the ORIGINAL intended capacity.
    // The only evaluation point is constant one. No population input exists.
    auto sigmoid = [&](double x) { return std::tanh((x / capacity - c.inflection) / c.width); };
    double zero = sigmoid(0);
    return Clamp((sigmoid(1) - zero) / (sigmoid(capacity) - zero), 0.0001, 1);
}
uint32 ScaledLevel(uint32 nativeLevel, uint32 anchorLevel, LevelPolicy const& p)
{
    if (p.mode == LevelMode::Native) return nativeLevel;
    uint32 lo = uint32(std::max(1, int(anchorLevel) - p.below));
    uint32 hi = uint32(std::min(255, int(anchorLevel) + p.above));
    return p.mode == LevelMode::Follow ? uint32(Clamp(nativeLevel, lo, hi)) : std::min(nativeLevel, hi);
}
static void Multiply(Factors& a, Factors const& b)
{
    a.health *= b.health; a.mana *= b.mana; a.armor *= b.armor;
    a.damage *= b.damage; a.cc *= b.cc;
}
Factors Calculate(double capacity, Profile profile, uint32 nativeLevel, uint32 targetLevel,
                  uint32 mapId, uint32 entry, Settings const& c)
{
    double q = OnePlayerCurve(capacity, c);
    Factors f;
    // Normal world creatures receive level handling only unless explicitly overridden.
    if (profile != Profile::Normal)
    {
        f.health = q; f.mana = std::sqrt(q); f.armor = std::pow(q, 0.25);
        f.damage = std::sqrt(q); f.cc = std::sqrt(q);
        Multiply(f, profile == Profile::Boss ? c.boss : c.trash);
        if (profile == Profile::Add && c.addsEnabled) Multiply(f, c.adds);
        f.health *= std::pow(c.difficulty, 0.5);
        f.damage *= c.difficulty;
        f.health = Clamp(f.health, c.minimum.health, c.maximum.health);
        f.mana = Clamp(f.mana, c.minimum.mana, c.maximum.mana);
        f.armor = Clamp(f.armor, c.minimum.armor, c.maximum.armor);
        f.damage = Clamp(f.damage, c.minimum.damage, c.maximum.damage);
        f.cc = Clamp(f.cc, c.minimum.cc, c.maximum.cc);
    }
    auto map = c.mapOverrides.find(mapId);
    if (map != c.mapOverrides.end()) Multiply(f, map->second);
    auto creature = c.creatureOverrides.find(entry);
    if (creature != c.creatureOverrides.end()) Multiply(f, creature->second);
    double level = Clamp(double(targetLevel) / std::max(1u, nativeLevel), 0.02, 5);
    f.health *= level * level; f.mana *= level * level;
    f.armor *= level; f.damage *= level;
    // Hard finite bounds remain effective after explicit overrides and level adjustment.
    f.health = Clamp(f.health, 0.0001, 100); f.mana = Clamp(f.mana, 0.0001, 100);
    f.armor = Clamp(f.armor, 0.0001, 100); f.damage = Clamp(f.damage, 0.0001, 100);
    f.cc = Clamp(f.cc, 0.01, 1);
    return f;
}
}
