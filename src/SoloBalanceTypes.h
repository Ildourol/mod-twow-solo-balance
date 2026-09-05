// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ObjectGuid.h"
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SoloBalance
{
enum class Scope { World, Dungeon, Raid, Disabled };
enum class Profile { Normal, Elite, Rare, RareElite, Boss, Trash, Add };
enum class LevelMode { Native, Downscale, Follow };
struct Factors
{
    double health = 1, mana = 1, armor = 1, damage = 1, cc = 1;
};
struct LevelPolicy
{
    LevelMode mode = LevelMode::Downscale;
    int below = 5, above = 2;
};
struct AddRule { uint32 boss = 0, add = 0, maximum = 0; };
struct Snapshot
{
    ObjectGuid guid;
    uint32 entry = 0, nativeLevel = 1, scaledLevel = 1;
    uint32 health = 1, mana = 0;
    int32 armor = 0;
    // Live modifier bases are retained separately from the resulting client fields.
    std::array<float, 3> statBases{};
    std::array<std::array<float, 2>, 3> weapons{};
    std::array<int32, 2> attackPower{};
    Factors factors;
    Profile profile = Profile::Trash;
    bool boss = false, add = false, applied = false, statDamage = false;
    uint32 anchorLevel = 0, generation = 0;
    uint64 lastSeen = 0;
};
struct SurvivalState
{
    // Sliding one-second window: ten conservative 100 ms buckets.
    std::array<double, 11> healed{};
    std::array<uint64, 11> bucketTime{};
    uint64 emergencyReady = 0;
    double manaCarry = 0, healthCarry = 0;
};
struct MapState
{
    ObjectGuid anchor;
    uint64 elapsed = 0;
    uint32 pendingMs = 0;
    std::map<ObjectGuid, Snapshot> creatures;
    SurvivalState survival;
    bool processingAuras = false;
    bool worldThreadsSafe = true;
    std::set<std::pair<uint32, ObjectGuid>> pendingRemoval;
};
char const* Name(Profile profile);
char const* Name(Scope scope);
char const* Name(LevelMode mode);
}
