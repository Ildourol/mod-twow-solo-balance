// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SoloBalanceTypes.h"
#include <memory>

namespace SoloBalance
{
struct Settings
{
    bool enabled = true, dungeons = true, raids = true, world = false, debug = false;
    std::array<bool, 5> worldRanks{{false, true, true, true, true}};
    std::array<double, 5> worldCapacity{{1, 3, 2, 5, 40}};
    uint32 interval = 500, maxTracked = 4096, expiry = 30000, generation = 0;
    double radius = 120, instanceRadius = 250;
    std::string preset = "normal";
    double difficulty = 1, inflection = 0.5, width = 0.3;
    Factors trash, boss, adds, minimum, maximum;
    bool addsEnabled = true, statDamage = false;
    LevelPolicy worldLevel, instanceLevel;
    bool leech = true, petLeech = true, cc = true, emergency = false;
    double leechPct = 0.05, leechCap = 0.05, manaPer5 = 0.05, healthPer5 = 0;
    double maxHit = 0.80;
    uint32 emergencyCooldown = 300, ccMax = 3000, charmMax = 1500;
    double xpModifier = 1.0, moneyModifier = 1.0;
    std::unordered_map<uint32, uint32> durations, stacks, mechanicCaps;
    std::unordered_set<uint32> removeAuras, bossEntries;
    std::unordered_map<uint32, Factors> mapOverrides, creatureOverrides;
    std::unordered_map<uint32, std::vector<AddRule>> addRules;
};
std::shared_ptr<Settings const> Config();
// Reads only the module-owned file beside mangosd.conf, in modules/.
// Parsing/reload is transactional. Invalid values leave the previous config active.
bool Reload(std::string& error);
std::string ConfigPath();
}
