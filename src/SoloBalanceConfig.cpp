// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SoloBalanceConfig.h"
#include "Config/Config.h"
#include "Log.h"
#include <atomic>
#include <cmath>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace SoloBalance
{
namespace
{
std::shared_ptr<Settings const> current = [] {
    auto c = std::make_shared<Settings>(); c->enabled = true; return c;
}();
std::mutex reloadMutex;
uint32 generation = 0;
std::string Clean(std::string s)
{
    auto first = s.find_first_not_of(" \t\r\n\"");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n\"") - first + 1);
}
std::vector<std::string> Split(std::string const& value, char separator)
{
    std::vector<std::string> result;
    if (value.empty()) return result;
    std::istringstream in(value); std::string token;
    while (std::getline(in, token, separator))
    {
        token = Clean(token);
        if (token.empty()) throw std::runtime_error("empty rule field");
        result.push_back(token);
    }
    if (value.back() == separator) throw std::runtime_error("trailing rule separator");
    return result;
}
double Number(std::string const& s, double lo, double hi, bool integral = false)
{
    size_t end = 0;
    double n = std::stod(s, &end);
    if (end != s.size() || !std::isfinite(n) || n < lo || n > hi || (integral && n != std::floor(n)))
        throw std::runtime_error("invalid numeric value: " + s);
    return n;
}
uint32 Id(std::string const& s) { return uint32(Number(s, 1, 2147483647, true)); }
struct Reader
{
    ::Config file;
    std::unordered_set<std::string> known;
    std::string Text(std::string const& key, char const* def)
    {
        known.insert(key);
        return Clean(file.GetStringDefault(key.c_str(), def));
    }
    double Num(char const* key, double def, double lo, double hi, bool integral = false)
    {
        std::string fallback = std::to_string(def);
        try { return Number(Text(key, fallback.c_str()), lo, hi, integral); }
        catch (std::exception const& e) { throw std::runtime_error(std::string(key) + ": " + e.what()); }
    }
    bool Bool(char const* key, bool def) { return Num(key, def ? 1 : 0, 0, 1, true) != 0; }
    LevelMode Mode(char const* key)
    {
        auto s = Text(key, "downscale");
        if (s == "native") return LevelMode::Native;
        if (s == "downscale") return LevelMode::Downscale;
        if (s == "follow") return LevelMode::Follow;
        throw std::runtime_error(std::string(key) + ": expected native, downscale, or follow");
    }
    void FactorsFor(char const* prefix, Factors& f, double lo, double hi)
    {
        f.health = Num((std::string(prefix) + ".HealthModifier").c_str(), f.health, lo, hi);
        f.mana = Num((std::string(prefix) + ".ManaModifier").c_str(), f.mana, lo, hi);
        f.armor = Num((std::string(prefix) + ".ArmorModifier").c_str(), f.armor, lo, hi);
        f.damage = Num((std::string(prefix) + ".DamageModifier").c_str(), f.damage, lo, hi);
        f.cc = Num((std::string(prefix) + ".CCModifier").c_str(), f.cc, lo, hi);
    }
    void Pairs(char const* key, std::unordered_map<uint32, uint32>& dest, uint32 lo, uint32 hi)
    {
        for (auto const& token : Split(Text(key, ""), ','))
        {
            auto parts = Split(token, ':');
            if (parts.size() != 2) throw std::runtime_error(std::string(key) + ": expected id:value");
            if (!dest.emplace(Id(parts[0]), uint32(Number(parts[1], lo, hi, true))).second)
                throw std::runtime_error(std::string(key) + ": duplicate ID");
        }
    }
    void Ids(char const* key, std::unordered_set<uint32>& dest)
    {
        for (auto const& token : Split(Text(key, ""), ','))
            if (!dest.insert(Id(token)).second) throw std::runtime_error(std::string(key) + ": duplicate ID");
    }
    void Overrides(char const* key, std::unordered_map<uint32, Factors>& dest, bool map)
    {
        for (auto const& token : Split(Text(key, ""), ','))
        {
            auto p = Split(token, ':');
            if (p.size() != 6) throw std::runtime_error(std::string(key) + ": expected id:health:mana:armor:damage:cc");
            Factors f;
            f.health = Number(p[1], 0.01, 10); f.mana = Number(p[2], 0.01, 10);
            f.armor = Number(p[3], 0.01, 10); f.damage = Number(p[4], 0.01, 10);
            f.cc = Number(p[5], 0.01, 1);
            uint32 id = uint32(Number(p[0], map ? 0 : 1, 2147483647, true));
            if (!dest.emplace(id, f).second) throw std::runtime_error(std::string(key) + ": duplicate ID");
        }
    }
};
}
std::shared_ptr<Settings const> Config() { return std::atomic_load(&current); }
std::string ConfigPath()
{
    auto filename = MaNGOS::Singleton< ::Config, ::Config::Lock>::Instance().GetFilename();
    auto slash = filename.find_last_of("/\\");
    std::string dir = (slash == std::string::npos ? "" : filename.substr(0, slash + 1));
    std::vector<std::string> candidates = {
        dir + "modules/mod-twow-solo-balance.conf",
        dir + "mod-twow-solo-balance.conf",
        dir + "modules/mod-twow-solo-balance.conf.dist",
        "modules/mod-twow-solo-balance.conf",
        "mod-twow-solo-balance.conf",
        "etc/modules/mod-twow-solo-balance.conf",
        "etc/mod-twow-solo-balance.conf"
    };
    for (auto const& path : candidates)
    {
        FILE* f = fopen(path.c_str(), "r");
        if (f)
        {
            fclose(f);
            return path;
        }
    }
    return dir + "modules/mod-twow-solo-balance.conf";
}
bool Reload(std::string& error)
{
    std::lock_guard<std::mutex> guard(reloadMutex);
    try
    {
        Reader r;
        std::string path = ConfigPath();
        bool fileLoaded = r.file.SetSource(path.c_str());
        if (!fileLoaded)
        {
            sLog.outString("[SoloBalance] Configuration file '%s' not found, applying default solo balance settings.", path.c_str());
        }
        auto c = std::make_shared<Settings>();
        c->preset = r.Text("SoloBalance.Preset", "normal");
        if (c->preset == "casual") { c->trash.health = c->boss.health = 0.8; c->trash.damage = c->boss.damage = 0.75; }
        else if (c->preset == "challenge") { c->trash.health = c->boss.health = 1.2; c->trash.damage = c->boss.damage = 1.25; }
        else if (c->preset != "normal" && c->preset != "custom") throw std::runtime_error("invalid SoloBalance.Preset");
        c->adds.health = c->adds.damage = 0.75;
        c->minimum.health = 0.025; c->minimum.mana = 0.10; c->minimum.armor = 0.25;
        c->minimum.damage = 0.10; c->minimum.cc = 0.25;
        c->maximum.health = c->maximum.mana = c->maximum.armor = c->maximum.damage = 2;
        c->maximum.cc = 1;
        c->enabled = r.Bool("TwowSoloBalance.Enable", true);
        c->dungeons = r.Bool("SoloBalance.Dungeons.Enable", true);
        c->raids = r.Bool("SoloBalance.Raids.Enable", true);
        c->world = r.Bool("SoloBalance.World.Enable", false);
        c->debug = r.Bool("SoloBalance.Debug", false);
        auto damageMode = r.Text("SoloBalance.Damage.Mode", "universal");
        if (damageMode != "universal" && damageMode != "stats") throw std::runtime_error("damage mode must be universal or stats");
        c->statDamage = damageMode == "stats";
        char const* ranks[] = {"Normal", "Elite", "Rare", "RareElite", "WorldBoss"};
        for (size_t i = 0; i < 5; ++i)
        {
            std::string prefix = std::string("SoloBalance.World.") + ranks[i];
            c->worldRanks[i] = r.Bool((prefix + ".Enable").c_str(), i != 0);
            if (i) c->worldCapacity[i] = r.Num((prefix + ".VirtualGroupSize").c_str(), c->worldCapacity[i], 1, 100, true);
        }
        c->interval = uint32(r.Num("SoloBalance.Scan.IntervalMs", 500, 100, 5000, true));
        c->radius = r.Num("SoloBalance.Scan.WorldRadius", 120, 20, 250);
        c->instanceRadius = r.Num("SoloBalance.Scan.InstanceRadius", 250, 20, 500);
        c->maxTracked = uint32(r.Num("SoloBalance.Scan.MaxTracked", 4096, 64, 20000, true));
        c->expiry = uint32(r.Num("SoloBalance.Scan.ExpiryMs", 30000, 5000, 300000, true));
        c->difficulty = r.Num("SoloBalance.Difficulty", 1, 0.25, 3);
        c->inflection = r.Num("SoloBalance.Curve.Inflection", 0.5, 0.1, 0.9);
        c->width = r.Num("SoloBalance.Curve.Width", 0.3, 0.1, 1);
        c->worldLevel.mode = r.Mode("SoloBalance.Level.World.Mode");
        c->instanceLevel.mode = r.Mode("SoloBalance.Level.Instance.Mode");
        c->worldLevel.below = int(r.Num("SoloBalance.Level.World.Below", 5, 0, 60, true));
        c->worldLevel.above = int(r.Num("SoloBalance.Level.World.Above", 2, 0, 10, true));
        c->instanceLevel.below = int(r.Num("SoloBalance.Level.Instance.Below", 5, 0, 60, true));
        c->instanceLevel.above = int(r.Num("SoloBalance.Level.Instance.Above", 2, 0, 10, true));
        r.FactorsFor("SoloBalance.Trash", c->trash, 0.01, 10);
        r.FactorsFor("SoloBalance.Boss", c->boss, 0.01, 10);
        r.FactorsFor("SoloBalance.Adds", c->adds, 0.01, 10);
        r.FactorsFor("SoloBalance.Clamp.Min", c->minimum, 0.001, 1);
        r.FactorsFor("SoloBalance.Clamp.Max", c->maximum, 0.01, 10);
        if (c->minimum.health > c->maximum.health || c->minimum.mana > c->maximum.mana ||
            c->minimum.armor > c->maximum.armor || c->minimum.damage > c->maximum.damage || c->minimum.cc > c->maximum.cc)
            throw std::runtime_error("minimum clamp exceeds maximum clamp");
        c->addsEnabled = r.Bool("SoloBalance.Adds.Enable", true);
        c->leech = r.Bool("SoloBalance.Sustain.HealthOnDamage.Enable", true);
        c->petLeech = r.Bool("SoloBalance.Sustain.HealthOnDamage.IncludePetDamage", true);
        c->leechPct = r.Num("SoloBalance.Sustain.HealthOnDamage.Pct", 0.05, 0, 1);
        c->leechCap = r.Num("SoloBalance.Sustain.HealthOnDamage.MaxHpPerSecond", 0.05, 0, 0.5);
        c->manaPer5 = r.Num("SoloBalance.Sustain.ManaPer5Seconds", 0.05, 0, 1);
        c->healthPer5 = r.Num("SoloBalance.Sustain.HealthPer5Seconds", 0, 0, 1);
        c->cc = r.Bool("SoloBalance.CC.Enable", true);
        c->ccMax = uint32(r.Num("SoloBalance.CC.MaxDurationMs", 3000, 100, 60000, true));
        c->charmMax = uint32(r.Num("SoloBalance.CC.CharmMaxDurationMs", 1500, 100, 60000, true));
        c->maxHit = r.Num("SoloBalance.Survival.MaxSingleHitPct", 0.80, 0.01, 1);
        c->emergency = r.Bool("SoloBalance.Survival.EmergencyProtection.Enable", false);
        c->emergencyCooldown = uint32(r.Num("SoloBalance.Survival.EmergencyProtection.CooldownSeconds", 300, 1, 86400, true));
        r.Pairs("SoloBalance.CC.DurationOverrides", c->durations, 100, 600000);
        r.Pairs("SoloBalance.CC.MechanicCaps", c->mechanicCaps, 100, 60000);
        for (auto const& p : c->mechanicCaps) if (p.first > 31) throw std::runtime_error("mechanic must be 1..31");
        r.Pairs("SoloBalance.Mechanics.MaxAuraStacks", c->stacks, 1, 255);
        r.Ids("SoloBalance.CC.RemoveAuraIds", c->removeAuras);
        r.Ids("SoloBalance.Mechanics.BossEntries", c->bossEntries);
        r.Overrides("SoloBalance.Mechanics.MapOverrides", c->mapOverrides, true);
        r.Overrides("SoloBalance.Mechanics.CreatureOverrides", c->creatureOverrides, false);
        for (auto const& token : Split(r.Text("SoloBalance.Adds.MaxActiveRules", ""), ','))
        {
            auto p = Split(token, ':');
            if (p.size() != 3) throw std::runtime_error("add rule must be bossEntry:addEntry:maxActive");
            AddRule rule; rule.boss = Id(p[0]); rule.add = Id(p[1]); rule.maximum = uint32(Number(p[2], 1, 100, true));
            auto& rules = c->addRules[rule.boss];
            for (auto const& old : rules) if (old.add == rule.add) throw std::runtime_error("duplicate boss/add pair");
            rules.push_back(rule);
        }
        c->xpModifier = r.Num("SoloBalance.Rewards.XPModifier", 1, 0, 10);
        c->moneyModifier = r.Num("SoloBalance.Rewards.MoneyModifier", 1, 0, 10);
        if (fileLoaded)
        {
            std::vector<std::string> sections;
            r.file.GetRootSections(sections);
            for (auto const& section : sections)
            {
                if (section != "TwowSoloBalance" && section != "SoloBalance") continue;
                std::vector<std::string> keys; r.file.GetKeys(section.c_str(), keys);
                for (auto const& key : keys) if (!r.known.count(key)) sLog.outDetail("[SoloBalance] unknown option: %s", key.c_str());
            }
        }
        c->generation = ++generation;
        std::atomic_store(&current, std::shared_ptr<Settings const>(c));
        error.clear();
        if (c->debug) sLog.outString("[SoloBalance] configuration generation %u loaded.", c->generation);
        return true;
    }
    catch (std::exception const& e)
    {
        error = e.what();
        sLog.outError("[SoloBalance] configuration rejected: %s", error.c_str());
        return false;
    }
}
}
