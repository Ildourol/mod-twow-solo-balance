# mod-twow-solo-balance

Standalone PvE conversion for a Penqle TortoiseWoW server with **exactly one real player**. That player may be a GM. Difficulty always evaluates a constant one-player target against the content's original intended capacity. Party size, raid membership and current population never enter the formula.

**Status: Full standalone module implementation under `modules/mod-twow-solo-balance/`. Cleanly compiled and linked into `mangosd` using CMake (`-DMODULES=static`) with MSVC 19.44 on Windows.**

## Delivery and target

The module lives directly in the core modules directory:

```text
tortoise-wow/modules/mod-twow-solo-balance/
```

Inspected target: [Penqle/tortoise-wow](https://github.com/Penqle/tortoise-wow), branch `main`, commit `17ee04d9acc95378b24703e2db4bb767df67756f`, on Windows using MSVC 2022 BuildTools and CMake.

Penqle discovers `src/` automatically, collects its sources/includes, and calls `Addmod_twow_solo_balanceScripts()`. Its existing module config installation discovers `conf/*.conf.dist`. No custom CMake, root loader changes, SQL, database changes, external libraries, bots, or core patches are needed or modified.

## Files

```text
src/SoloBalanceTypes.h
src/SoloBalanceConfig.h
src/SoloBalanceConfig.cpp
src/SoloBalanceScaling.h
src/SoloBalanceScaling.cpp
src/SoloBalanceManager.h
src/SoloBalanceManager.cpp
src/SoloBalanceSurvival.h
src/SoloBalanceSurvival.cpp
src/SoloBalanceMechanics.h
src/SoloBalanceMechanics.cpp
src/SoloBalanceScripts.cpp
src/SoloBalanceCommands.cpp
src/mod_twow_solo_balance_loader.cpp
conf/mod-twow-solo-balance.conf.dist
README.md
COMMANDS.txt
VALIDATION.md
LICENSE
```

All deliverables are inside the requested `module` directory. Nothing was installed into or modified in `tortoise-wow` or the reference module.

## Installation

These steps assume you have a working [TortoiseWoW](https://github.com/Penqle/tortoise-wow) source checkout, initialized core databases, and the server data files for your realm. Use a core revision compatible with this module. Run all build and SQL commands from the core source root, the directory containing the core's `CMakeLists.txt`.


### 1. Add the module and build on Linux

Use Git, CMake, a C++17 compiler, and the core's build dependencies. On Debian or Ubuntu, a typical starting point is:

```bash
sudo apt update
sudo apt install git cmake build-essential libace-dev default-libmysqlclient-dev libssl-dev zlib1g-dev
```

Use your distribution's `mysql` or `mariadb` command-line client if importing SQL manually. Keep any additional dependency and CMake settings required by your core and other enabled modules.

```bash
cd /path/to/tortoise-wow
git clone https://github.com/Ildourol/mod-twow-solo-balance.git modules/mod-twow-solo-balance
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/server-stage" -DMODULE_MOD_TWOW_SOLO_BALANCE=static
cmake --build build --parallel
cmake --install build
```

### 2. Add the module and build on Windows

Install Git for Windows, CMake 3.21 or later, and Visual Studio 2022 or its Build Tools with **Desktop development with C++** and a Windows SDK. Open PowerShell with Git and CMake on `PATH`. Use an x64 build and the dependency setup of your working core.

```powershell
Set-Location 'C:\src\tortoise-wow'
git clone https://github.com/Ildourol/mod-twow-solo-balance.git modules/mod-twow-solo-balance
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 "-DCMAKE_INSTALL_PREFIX=$PWD/server-stage" -DMODULE_MOD_TWOW_SOLO_BALANCE=static
cmake --build build-vs --config Release --parallel
cmake --install build-vs --config Release
```

Check each command for errors before continuing. Both builds include `mangosd`. If the configure output reports an unused module switch, verify the source placement before building. The two build sections are platform alternatives. If you already have a module checkout, use it in place of the clone. A full directory copy, a Linux symlink, or a Windows directory junction works as long as `modules/mod-twow-solo-balance/src/` is directly below the module directory. The module directory name determines the CMake switch and loader symbol. If reusing a build directory, keep its generator and architecture and preserve the switches for your other modules. Reconfigure after adding the module and verify CMake lists it as static.

The staged world server is normally `server-stage/bin/mangosd` on Linux and `server-stage/mangosd.exe` on Windows. Stop the running world server before deploying the rebuilt executable and any required runtime libraries or module assets. Preserve the live configuration and data files. The paths below assume the standard staged layout. For an existing deployment, use the directory containing the active `mangosd.conf`, including a custom path passed via `-c`.

### 3. Set up the database

This module ships no SQL migrations. The initialized core databases are sufficient.

### 4. Deploy and enable the configuration

Create `mod-twow-solo-balance.conf` inside the `modules/` subdirectory beside the active `mangosd.conf`. For a first install using the example staging directory, the following commands copy the shipped template. If an active module config already exists, retain it and merge new settings from the template.

**Linux**

```bash
mkdir -p server-stage/etc/modules
cp -n "modules/mod-twow-solo-balance/conf/mod-twow-solo-balance.conf.dist" "server-stage/etc/modules/mod-twow-solo-balance.conf"
```

**Windows (PowerShell)**

```powershell
New-Item -ItemType Directory -Force 'server-stage/modules' | Out-Null
if (-not (Test-Path 'server-stage/modules/mod-twow-solo-balance.conf')) {
    Copy-Item 'modules/mod-twow-solo-balance/conf/mod-twow-solo-balance.conf.dist' 'server-stage/modules/mod-twow-solo-balance.conf'
}
```

Keep the template's section header and confirm `TwowSoloBalance.Enable = 1` in the active file. Start the rebuilt server with your normal launcher and active `mangosd.conf`. Check the startup log for module configuration, database, and dependency errors.

### Verify the installation

With 1 real player on a test realm, enter a dungeon and run `.solo info` with a developer account. Verify the active scope and see the runtime settings below for solo raid entry and optional world scaling.

## Runtime setup

Keep the `[TwowSoloBalance]` section in the deployed configuration.

The core loads installed module files. SoloBalance also reads its own file using a separate native `Config` object, which makes `.solo reload` transactional and avoids reloading or writing the main server configuration. Unknown keys, malformed values, invalid ranges, duplicate rule IDs and inverted clamps reject a reload. Existing settings remain active after failure; startup failure leaves this module disabled. Use one section and one assignment per option; the underlying INI importer may collapse duplicate assignments before validation.

In your external server configuration, ensure:

```ini
Instance.IgnoreRaid = 1
```

The module does not set that option, create groups, grant raid entry, disable GM immunity, change account security, or modify any runtime file. GM characters are eligible; active GM mode still prevents normal aggro in the core. For realistic combat tests, use a privileged character with normal combat/aggro behavior and damage immunity disabled through your usual server controls.

World mode is optional and starts disabled. It requires `MapUpdate.Continents.MTCells.Threads` to be `0` or `1` **at map creation and currently**. The checked-out core defaults to `0`. Restart after changing this external option. With more threads, world conversion fails closed and `.solo info` explains why; instances remain supported. See the threading evidence below. The module never changes this server setting.

## Scaling

For capacity `N > 1`, define `S(x) = tanh((x/N - Inflection)/Width)` and evaluate:

```text
q = clamp((S(1) - S(0)) / (S(N) - S(0)), 0.0001, 1)
```

Capacity one returns one. The normalization anchors zero and full capacity. It is a fresh implementation of the general sigmoid concept, not a copied population-dependent formula.

Instances use `DungeonMap::GetMaxPlayers()` as static metadata, including custom capacities. World rank metadata defaults to elite=3, rare=2, rare-elite=5 and world-boss=40. These are initial tuning values, not encounter balance claims.

Before profile modifiers: health=`q`, mana=`sqrt(q)`, armor=`q^0.25`, damage=`sqrt(q)`, CC=`sqrt(q)`. Boss and trash profiles are separate. Temporary summons receive trash factors multiplied by add factors when enabled. Boss rank, explicitly configured boss entries, and boss entries in explicit add rules identify bosses; rare elites are not automatically instance bosses.

Precedence is: preset defaults -> explicitly provided detailed profile values -> capacity calculation/profile/add modifiers -> global difficulty -> configurable clamps -> map override -> creature override -> level model -> hard finite bounds. Override factors multiply. A map or creature override does not bypass scope/rank/friendly exclusions. Difficulty weights health by its square root and damage directly; it does not inflate every stat equally.

| Preset | Trash/boss health default | Trash/boss damage default |
|---|---:|---:|
| casual | 0.8 | 0.75 |
| normal | 1.0 | 1.0 |
| challenge | 1.2 | 1.25 |
| custom | 1.0 | 1.0 |

Detailed trash/boss settings are commented in the template so changing the preset has an effect. Uncommenting one replaces that preset default. Other profile stats default to one; adds default to health/damage=0.75. Rank selectors are independent. Normal world creatures, if opted in, skip capacity/profile/difficulty nerfs and receive level handling plus any explicit overrides.

### Level policy and snapshots

`native` preserves level. `downscale` only lowers creatures above player level plus `Above`. `follow` clamps into `[player-Below, player+Above]`. World and instance policies are separate. With player level 30 and `Above=2`, downscale maps 26/30/32/37/55 to 26/30/32/32/32.

`Creature::SelectLevel` rerolls native level and reinitializes health, mana and weapons; it is deliberately not called during scaling. A deterministic ratio of target/native level scales health and mana quadratically, armor and damage linearly. This is an approximation, not a class-specific creature-stat table. Hard level-ratio bounds are 0.02..5; final HP/mana/armor/damage factors are 0.0001..100 and CC factors 0.01..1. Signed core numeric limits are respected.

Snapshots are keyed by `ObjectGuid`, inside separate map-ID/instance-ID state, and contain native level, live max HP/mana/armor, modifier bases, all three primary weapon ranges, both AP fields, classifications, factors, generation and timestamps. No world-object pointer persists between callbacks. Reloads use the original snapshot and preserve current health/mana percentages. Dead creatures stay dead. Ordinary deaths restore and retire state; periodic discovery also handles dead objects, unloaded GUIDs, entry changes and detectable same-entry native stat resets.

Discovery runs every 500 ms by default, within 120 world yards or 250 instance yards, over loaded grid and world containers with `dont_load=true`. Initial combat entry also attempts scaling for newly engaged creatures. Tracking is limited to 4,096 creatures per map. Unseen idle objects expire after 30 seconds and are restored before their snapshots are discarded. Leaving the map restores tracked objects; map destruction retires the map state. First-time capture may occur in combat, but subsequent config/level stat changes wait until that creature leaves combat. Disabling scope/master restores at the next scan, including combatants.

Player pets/controlled units, player-owned summons, totems, critters, invisible triggers, nonattackable/nonselectable/player-immune flags and friendly creatures are excluded. Neutral attackable creatures can qualify. GM/charm eligibility uses the character's race faction and reputation because active GM mode changes core reaction results to neutral. Two players on the same map disable that map's conversion; this safety check never changes a factor. It is not a global online-player enforcement system.

### Damage modes

**`universal` (default):** the final `OnDamage` hook applies the captured damage factor exactly once to attacks, weapon spells, magic, periodic damage and script damage that reaches `DealDamage`, when the recipient is the anchor or their directly owned unit. Weapon/AP fields remain native in this mode. This deliberate choice gives the broadest coverage and avoids the ambiguity of weapon-derived spells in earlier hooks. Creature health/mana/armor/level still use snapshots. The selected creature's diagnostic factor describes outgoing damage, not necessarily its client weapon fields.

**`stats` (optional):** changes main-hand, off-hand and ranged primary base ranges, both AP fields, and calls `UpdateDamagePhysical` for all three attacks. The core's additional AP term is compensated in the weapon values. Magic/periodic paths through `ModifySpellDamageTaken` use the damage factor; spells containing weapon-damage effects are excluded there. The final hook only supplies survival protection in this mode. Mode changes use each creature's applied mode until deferred rescaling completes.

The optional mode is less complete: spell paths skipping `SpellDamageBonusTaken`, flat melee-class spell damage, hybrid weapon/nonweapon effects, extra weapon school components, later AP/stat recalculations, and arbitrary script mutations need encounter testing. It also changes physical damage against NPCs. Use universal mode for the initial solo conversion. Neither mode intercepts direct `SetHealth`, `ModifyHealth`, `Kill` or scripted state transitions that bypass `DealDamage`.

## Survival and mechanics

Leech heals the player for 5% of qualifying damage by default, including optional direct pet/guardian-owner credit. It ignores self/environmental damage, unrelated content and evade targets, limits credit by target HP/invincibility floor, never exceeds missing health, and uses a conservative rolling one-second cap of 5% player max HP. Eleven 100-ms buckets avoid a fixed-window AoE burst exploit. Healing directly changes health and does not recursively cast a healing spell.

The damage hook receives normally mitigated damage before health application. It is not an after-damage receipt: special downstream core adjustments and other modules can still alter the hit. Thus leech tracks eligible hook damage, excluding overkill, rather than an independently measured final health delta. Combat-log packets/procs may already contain the earlier damage value. The core's special downstream exceptions are documented in `VALIDATION.md`.

Mana sustain defaults to 5% of max mana per five seconds; secondary health sustain defaults to zero. Fractional amounts carry between scans. Both require the player to be alive, in combat, and connected through victim/attacker ownership to a tracked living enemy. No passive out-of-combat regeneration is added. Stall catch-up is bounded to five seconds. Leech budgets and emergency cooldowns survive normal map changes and relogs until server restart; up to 64 character records are retained for accidental multi-character test use.

Incoming qualifying player damage is capped at 80% of maximum health. Emergency protection is off by default; when enabled and ready, it leaves one HP on a lethal hit and consumes a 300-second cooldown. It is not resurrection and does not affect falling, environmental damage, self damage or PvP. Pet incoming damage is scaled, but the player's one-hit/emergency protections are not granted to pets.

Hostile aura control supports charm/possession, stun, fear, confusion, root, silence and matching sleep/freeze/polymorph/banish/horror/sap mechanic metadata. CC cap defaults to 3,000 ms multiplied by the creature's CC factor, with an additional 1,500-ms charm upper bound. Overrides and mechanic caps can only shorten durations. Beneficial auras and unrelated casters are ignored. Removal and stack reduction happen on the next scan, outside `ApplyModifier`; the initial application and its side effects still happen. This cannot prevent an instant charm phase reset or an instant stack-trigger effect.

NPC healing through `WorldObject::DealHeal` is multiplied once by the recipient's scaled health factor. Player-owned healers and player recipients are excluded. Direct script health setters, gameobject heal sources without a Unit, and core regeneration bypass this hook. Percentage-of-current-max-HP heals can receive an additional reduction because the hook exposes neither the spell nor whether the amount was already proportional; tune narrow encounter rules after observing actual behavior.

### Rule formats

All encounter-specific lists ship empty. Only add IDs verified against the exact encounter source and/or your live database. The module parses and validates numeric ranges and duplicates; it does not certify that a user-supplied ID exists or has the intended encounter meaning.

| Setting suffix | Format | Effect |
|---|---|---|
| `CC.DurationOverrides` | `spellId:durationMs,...` | Additional duration upper bounds |
| `CC.MechanicCaps` | `mechanicId:durationMs,...` | Additional caps using native mechanic IDs |
| `CC.RemoveAuraIds` | `spellId,...` | Deferred removal of the exact hostile caster's holder |
| `Mechanics.MaxAuraStacks` | `spellId:maximum,...` | Deferred stack reduction |
| `Mechanics.BossEntries` | `creatureEntry,...` | Explicit boss profile |
| `Mechanics.MapOverrides` | `mapId:health:mana:armor:damage:cc,...` | Map stat factors; map zero is valid |
| `Mechanics.CreatureOverrides` | `entry:health:mana:armor:damage:cc,...` | Additional entry factors |
| `Adds.MaxActiveRules` | `bossEntry:addEntry:maximum,...` | Exact summoner-GUID count rules |

Each suffix has the `SoloBalance.` prefix. Add limiting considers tracked loaded living `TemporarySummon` objects, a living tracked boss fighting the anchor/pet, an exact configured entry pair, and the summon-owner GUID. It keeps the lowest GUIDs up to the limit and calls `UnSummon` on excess objects. Normal spawn adds, untracked/out-of-range summons, nested ownership and scripted bookkeeping not represented by `TemporarySummon` are not globally limited. Unsummon callbacks can have encounter consequences; test every enabled pair. There is no arbitrary global add purge.

`CanPrepare` and general gameobject update/add callbacks are declared but not dispatched in this target. No fake spell-suppression or gameobject-autoactivation options are exposed. Simultaneous switches, split-room mechanics, threat/target-count conditions and encounter script state still need narrow, verified module-visible solutions. No boss-specific rules or completion claims are included.

## Rewards

By default, XP, money, loot and drop rates remain completely unchanged (`SoloBalance.Rewards.XPModifier = 1.0`, `SoloBalance.Rewards.MoneyModifier = 1.0`). Rewards are never scaled from player count. The player XP hook recomputes native-level versus scaled-level gain using the unchanged core formula to offset level scaling while retaining the caller's reward factor. Its temporary level field change is synchronous and restored before returning. Optional fixed XP and money multipliers (`0.0` to `10.0`) are configurable under `SoloBalance.Rewards.*`. Loot tables are never altered.

**Pet XP still follows the core's scaled-level calculation**, because `Pet::GivePetXP` has no module hook here. Use native level mode if identical pet XP is required. External scripts or modules that supply atypical XP amounts need validation. This is a specific remaining limitation of reward preservation.

## Configuration reference

Every supported key, accepted range, default, unit and effect is documented next to its assignment in [the config template](conf/mod-twow-solo-balance.conf.dist). The supported groups are master/preset/difficulty, content scopes, five world rank selectors/four virtual capacities, bounded discovery, two level policies, sigmoid parameters, damage mode, separate trash/boss/add stat factors, five min/max clamps, capped leech/pet credit, combat resources, CC/aura rules, one-hit/emergency protection, map/entry/add rules, fixed rewards and diagnostics. There are no compatibility aliases. An unsupported key rejects reload rather than silently doing nothing.

## Commands and smoke tests

See [COMMANDS.txt](COMMANDS.txt).
- `.solo info`: requires `SEC_DEVELOPER` (3)+; prints module state, anchor player info, scope, level policy, world ranks, tracked creature count, sustain, survival, and mechanics rule counts.
- `.solo creature`: requires `SEC_DEVELOPER` (3)+; select a creature first; shows profile, level, HP, multipliers, and overrides.
- `.solo rescale`: requires `SEC_DEVELOPER` (3)+; immediately forces rescaling on the selected target or all tracked creatures on the current map.
- `.solo reload`: requires `SEC_ADMINISTRATOR` (4)+ (or console); transactionally reloads module config.

Output is private. The core only allows `ChatHandler` member pointers in `ChatCommand`; this module uses its verified `AllCommandScript` interceptor instead, with its own explicit permission checks. Consequently standard `.help solo` table discovery is not provided.

On a disposable runtime after separate compilation/link verification:

1. Start with supplied defaults, one character and no other scaling/leech module enabled. Run `.solo info` in a dungeon and a raid. Verify anchor, scope and native capacity behavior.
2. Select trash and a boss; run `.solo creature`. Compare full HP, partial HP rescale, mana, armor and actual incoming melee/magic/DoT health deltas. Repeat with a privileged GM character.
3. Reload several times; confirm native snapshots and factors do not compound. Change level, reset combat, kill/respawn, leave/re-enter, unload/reload grids, and repeat. Check that corpses stay dead and transformations do not inherit another entry's baseline.
4. Check player damage and pet-only damage leech, AoE/DoT rolling cap, missing-health limit, combat-only mana and optional health sustain. Test one-hit and emergency cooldown behavior on non-hardcore test characters.
5. If enabling world scope, use serialized cell updates and check each rank separately; test friendly NPCs, critters, owned summons and totems as exclusions. Add a second character to the map and verify safety restoration.
6. Verify CC duration, charm release, beneficial/PvP exclusions, and only then add individually source-verified aura/stack/add rules. Test multi-effect/refresh/stack auras and summoner callbacks.
7. Test native-level player XP and the documented pet-XP difference. Confirm money/loot stay on core paths. Set an invalid numeric option and verify reload rejects it without losing previous settings.
8. Disable the master switch and reload. After one scan verify restored HP percentage, mana, armor, level and optional weapon/AP fields. Restart after changing server thread settings.

## Hook verification and limitations

See [VALIDATION.md](VALIDATION.md) for declarations, dispatch lines, call-order evidence, source checks and the explicit unbuilt status. The unresolved limits above are part of this delivery, especially pet XP, instantaneous aura effects, direct scripted health changes, optional stat-damage coverage, world cell threading, arbitrary scripted stat resets and encounter-specific multiplayer mechanics. No live raid was tested.

## License and references

New module source is licensed under AGPL-3.0-or-later; [LICENSE](LICENSE) contains the license text. Behavior was reimplemented against the checked-out APIs. No implementation was copied from the local GPLv2-declared AutoBalance reference.

API references inspected: [Penqle autoscale](https://github.com/Penqle/tw-mod-autoscale) (MIT, including its license) and [Penqle leech](https://github.com/Penqle/tw-mod-leech) (root MIT license, but its source header declares AGPLv3). They were used to verify native patterns, not imported. The local [Turtle AutoBalance reference](https://github.com/Ildourol/mod-twow-autobalance) supplied architecture/math context; its README declares GPLv2. The core root license is AGPLv3. No license compatibility assumption was used to copy their implementation.
