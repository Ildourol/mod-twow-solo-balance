# Source validation report

Date: 2026-09-05. Target: `Penqle/tortoise-wow`, local branch `main`, commit `17ee04d9acc95378b24703e2db4bb767df67756f`.

## Scope and build status

The module is fully implemented under `modules/mod-twow-solo-balance/`.
The build and link were executed and verified against the target checkout:
- **Build toolchain**: CMake 3.31.6, MSVC 19.44.35228.0 (Visual Studio 2022 BuildTools), Windows SDK 10.0.26100.0, x64 architecture.
- **Dependencies**: ACE from `C:/vcpkg/installed/x64-windows`.
- **Module discovery**: Discovered automatically by Penqle's module system as static (`MODULES=static`), loader `Addmod_twow_solo_balanceScripts` configured and invoked.
- **Compilation**: Both `modules` target (`modules.lib`) and full `mangosd` target compiled with 0 errors.
- **Linking**: `mangosd` linked successfully to `bin/Release/mangosd.exe` (10,452,480 bytes) with 0 unresolved symbols.
- **Git integrity**: No core source files, root CMake files, or database files modified. Clean git diff.

## Verified declarations and actual calls

Paths and line numbers in this table refer to the unchanged target checkout above, not a moving remote branch. All hook declarations are in `src/game/ScriptObjects.h`.

| Hook used | Declaration line | Actual call site | Why it matters |
|---|---:|---|---|
| `WorldScript::OnAfterConfigLoad` | 96 | `src/game/World.cpp:889` | Reads module-owned config after core config loading |
| `WorldScript::OnStartup` | 103 | `src/game/World.cpp:2445` | Startup initialization even when initial config load preceded registration |
| `AllMapScript::OnCreateMap` | 371 | `src/game/Maps/MapManager.cpp:180`, also 612/639/674/696 | Captures map creation-time cell-thread safety |
| `AllMapScript::OnMapUpdate` | 375 | `src/game/Maps/Map.cpp:872` | Runs before session/player/cell updates; bounded discovery and deferred aura work |
| `AllMapScript::OnPlayerLeaveAll` | 374 | `src/game/Maps/Map.cpp:1139` | Runs before player removal/unlink, allowing restoration and GUID budget transfer |
| `AllMapScript::OnDestroyMap` | 372 | `src/game/Maps/MapManager.cpp:266`, also 446/494/649 | Retires map-owned value state |
| `UnitScript::OnDamage` | 483 | `src/game/Objects/Unit.cpp:726` | Modifiable reference at start of `DealDamage`, before health application |
| `UnitScript::OnHeal` | 482 | `src/game/Objects/Object.cpp:4251` | Modifiable heal amount before `ModifyHealth` in `DealHeal` |
| `UnitScript::OnAuraApply` | 487 | `src/game/Spells/SpellAuras.cpp:926` | After native aura handler, while holder/aura remain in use |
| `UnitScript::OnUnitEnterCombat` | 490 | `src/game/Objects/Unit.cpp:6576` | Initial capture for newly engaged creatures, after combat flag is set |
| `UnitScript::OnUnitDeath` | 492 | `src/game/Objects/Unit.cpp:1322` | After `SetHealth(0)` and `SetDeathState(JUST_DIED)`; safe non-resurrecting cleanup |
| `UnitScript::ModifySpellDamageTaken` | 485 | `src/game/Objects/Unit.cpp:5752` | Optional stats mode only; narrower spell coverage than universal damage |
| `PlayerScript::OnGiveXP` | 167 | `src/game/Objects/Player.cpp:3599` | Native-level player XP correction before rested/player-level processing |
| `PlayerScript::OnMoneyChanged` | 166 | `src/game/Objects/Player.cpp:16185` | Optional fixed money multiplier in solo combat |
| `AllCommandScript::CanExecuteCommand` | 358 | `src/game/Chat/Chat.cpp:1587` (dispatch block starts 1585) | Intercepts a new module command without adding `ChatHandler` members |

Only the required World/Unit/Player hook enums are enabled. `AllMapScript` and `AllCommandScript` do not expose individual enum selection in this revision. No database-bound encounter or spell scripts are registered.

## Damage and healing call order

`Unit::DealDamage` starts at `Unit.cpp:717`. It checks the player's general damage disable flag before dispatching `OnDamage` at line 726. Normal attack callers have already calculated damage/mitigation; the direct attack reaches `DealDamage` at line 1875. Periodic processing in `SpellAuras.cpp` calculates bonuses, absorb/resist and the final tick before calling `DealDamage`. Thus the hook can reduce the actual damage variable, unlike a post-event notification.

`DealDamage` later processes fear/interrupt effects, duels, combat entry, damage-origin accounting, hardcoded hardcore/avoidance adjustments, the lethal branch and final health subtraction at `Unit.cpp:913`. `Kill` begins at line 1071. Consequently:

- Universal damage scaling is applied once in `OnDamage`. The earlier spell hook is inert for a creature using universal mode.
- Incoming player single-hit/emergency caps are before health subtraction, not resurrection. Core conditions and other modules can still affect later behavior.
- Leech is based on the hook's mitigated amount, bounded by the victim's available health above its invincibility floor. It is not a guaranteed post-health-delta receipt. The native hardcore-specific adjustment and attacker avoidance branch after the hook can change the actual amount. No invented encounter IDs are used to imitate those branches.
- Existing damage logs and procs may have been emitted before `DealDamage`; universal scaling does not rewrite those packets. Compare actual health changes in tests.

`SpellDamageBonusTaken` at `Unit.cpp:5722` has early returns for missing inputs, `DIRECT_DAMAGE` and ignore-damage-taken flags, before its hook at 5752. Physical/weapon paths often use `MeleeDamageBonusTaken` instead. `SpellEffects.cpp:3305` is an example of a weapon effect that also invokes the spell-bonus function; optional stats mode skips spells with weapon effects to prevent a second multiplier. These facts justify universal mode as the default and the documented optional-mode limitations.

`WorldObject::DealHeal` at `Object.cpp:4239` first invokes AI `HealedBy`, then `OnHeal` at 4251, then `ModifyHealth` at 4255. SoloBalance scales this final heal once; it does not also use `ModifyHealReceived` (`Unit.cpp:5974`). The hook dispatch is conditional on a Unit healer. Scripts calling `SetHealth`/`ModifyHealth` directly and non-Unit healers are not intercepted. The hook has no spell metadata or percentage-heal discriminator.

## Other source evidence

- `modules/README.md`, `modules/CMakeLists.txt`, `modules/ModulesLoader.cpp.in.cmake`, and all files under `modules/templates/basic/` were inspected. Source discovery, sanitized loader naming, config installation and absence of a custom CMake requirement were checked.
- `Config.cpp:207` loads native module configs from the main config directory's `modules/` folder. The module mirrors this path with a separate `Config` instance for its own transactional reload. Core getters alone would silently coerce bad numeric strings, so explicit full-string/range/finite checks are used.
- `Map.cpp:2328` returns `i_mapEntry->maxPlayers` in `DungeonMap::GetMaxPlayers`. It is capacity metadata, not a runtime player count.
- `CellImpl.h:200` implements `VisitAllObjects`; `dont_load=true` calls `SetNoCreate` before visiting grid and world containers. `GridNotifiersImpl.h:539` supplies the native creature-list traversal.
- `Creature.cpp:1576` implements `SelectLevel`, including random level selection, health/mana initialization and weapon replacement. It is not a safe selected-level-only helper. The module uses a deterministic snapshot model instead.
- `StatSystem.cpp:757/763/769` handles creature armor/max-health/max-power. `UpdateAttackPowerAndDamage` at 789 resets base AP from the template; `UpdateDamagePhysical` at 828 includes the extra `0.7 + 0.3 * currentAP/templateAP` factor. Optional stats mode compensates the initial factor, but later aura/script recalculation remains a testing limitation.
- `Creature.h:648` identifies a world boss by `CREATURE_ELITE_WORLDBOSS`. Exact normal/elite/rare/rare-elite/world-boss enum names were checked against core headers. Rare elite is not synonymous with instance boss.
- `Object.cpp:3432` onward implements faction reactions; its GM branches return neutral. `Player.cpp:3404` changes GM faction; `Player::GetFactionForRace` is at 7769. Eligibility therefore uses race faction and reputation/forced-rank/at-war checks for GM/charm cases.
- `Aura::ApplyModifier` at `SpellAuras.cpp:896` sets holder/aura in-use flags before the handler and hook. `SetAuraMaxDuration` at 7448 updates permanent-state handling. Removal and stack reduction are deferred and resolve exact spell/caster identifiers again.
- `TemporarySummon.h` exposes `GetSummonerGuid` and `UnSummon`. Count rules use exact owner GUID and entry pairs, not global summon suppression.
- `Formulas.h:102` implements synchronous `MaNGOS::XP::Gain`, without a FormulaScript call. `Player.cpp:21900` and `Unit.cpp:1214/1216` show reward calculation before the death callback. `Unit.cpp:8672` makes creature `SetLevel` a field update. The XP hook temporarily evaluates the native level and restores the actual level before returning. Pet XP is awarded separately at `Player.cpp:21921` and has no corresponding module hook; identical pet XP with level scaling remains unresolved.
- `Chat.h:62` defines commands with a `ChatHandler` member-function pointer, not a free callback. `Chat.cpp:1585` dispatches the usable interceptor. The module enforces native security constants `SEC_DEVELOPER`/`SEC_ADMINISTRATOR` itself. `Common.h:184` defines the native account levels; there is no `SEC_GAMEMASTER` in this core.

## Threading decision

`MapManager.cpp:393/399` launches separate map workloads and waits for them before returning. State is separated by map/instance. The registry mutex only covers shared value ownership; no core call is made under it. Config snapshots are immutable and atomically published.

Within a continent, `Map.cpp:663` and `UpdateActiveCellsAsynch` at 691 can update different cell regions concurrently. `Map.cpp:144/146` creates cell/other worker pools, and the cell pool size depends on `MapUpdate.Continents.MTCells.Threads`. Configuring more than one cell-update thread would allow unrelated creature death/combat hooks to touch one map's state concurrently. Therefore optional world scope is disabled unless the setting was <=1 at map creation and remains <=1. Core default is zero (`World.cpp:1283`). Capture at map creation prevents a live config reload from falsely certifying an already-created worker pool as serial.

Commands are world-phase work (`Protocol/Opcodes.cpp:205`, `PACKET_PROCESS_WORLD`); `World.cpp:2537` updates sessions before `sMapMgr.Update` at 2571. Diagnostics therefore use the core's synchronized command phase. The module creates no worker, network client, database job or persistent world-object pointer.

## Proven absent dispatches / deliberate gaps

Repository-wide searches found these only in `ScriptObjects.h`, with no target core call sites:

```text
OnAllCreatureUpdate / OnCreatureAddWorld / OnCreatureRemoveWorld
AllSpellScript::CanPrepare
OnGameObjectAddWorld / OnGameObjectUpdate
```

Creature lifecycle uses bounded discovery plus verified combat/death hooks. No success is claimed for selected spell suppression or generic gameobject autoactivation. No imaginary hook or core patch was added. Gameobject/script-state mechanics need encounter-specific source evidence; even a callable `Use` method does not establish that autoactivation is semantically safe.

Additional limits: first-hit discovery latency, direct/scripted health setters, direct instantaneous same-entry resets not recognizable from native fields, later arbitrary stat mutations, already-executed aura effects, loaded-only add count limits, other damage modules, and untested encounter bookkeeping. Default encounter-ID lists are empty. No dungeon/raid completion was live-tested.

## Checks actually performed

Read-only target inventory before and after:

```powershell
git -C tortoise-wow status --short
git -C tortoise-wow branch --show-current
git -C tortoise-wow rev-parse HEAD
git -C tortoise-wow remote -v
rg --files -uu -g AGENTS.md -g CMakeCache.txt -g '*PLAYBOOK*'
git -C tortoise-wow diff --check
git -C mod-twow-autobalance status --porcelain=v1
```

Results: core and reference clean; target commit/branch as above; no existing build tree found. Source was also reviewed with focused `rg` and PowerShell file reads against declarations and actual call sites.

For each new source file, `git -c core.autocrlf=false diff --no-index --check -- /dev/null <file>` produced no whitespace errors. Exit code one for a new-file diff was treated as differences, not as a compiler/test result. Source search found no `GetPlayersCountExceptGMs`, `sConfigMgr`, foreign `AC_` API, bot dependency, `std::thread` or `std::async`. No SQL file is present.

Custom **source-text checks only** verified:

- All 78 supported config keys have documented accepted values, defaults, units and effects: 68 active defaults and 10 commented preset-sensitive overrides. No undocumented or unknown template key; one valid section and supported assignment syntax.
- 113 called method names were cross-checked against target headers (module Reader helper names excluded); a target-specific `GetRace` spelling issue was corrected. Presence alone does not verify C++ overload resolution or linkage.
- 54 referenced core hook/enum constants were found in target headers.
- Braces, brackets and parentheses balance in all 14 source files after stripping comments and strings. This is not a C++ parser.
- Separate PowerShell arithmetic evaluated the documented sigmoid over 900 capacity/parameter combinations (capacities 1..100 and the parameter min/default/max combinations), checking finite bounds and non-increasing output, plus a known five-player value. The required downscale example also matched. These are arithmetic checks of the model, not execution of the C++ implementation.

Normal-preset trash/boss arithmetic, before level adjustment or explicit overrides:

| Intended capacity | Raw curve | HP factor | Damage factor |
|---:|---:|---:|---:|
| 1 | 1.000000 | 1.000000 | 1.000000 |
| 3 | 0.228994 | 0.228994 | 0.478533 |
| 5 | 0.091029 | 0.091029 | 0.301710 |
| 10 | 0.032782 | 0.032782 | 0.181059 |
| 15 | 0.019611 | 0.025000 | 0.140041 |
| 20 | 0.013941 | 0.025000 | 0.118072 |
| 25 | 0.010802 | 0.025000 | 0.103934 |
| 40 | 0.006438 | 0.025000 | 0.100000 |
| 100 | 0.002457 | 0.025000 | 0.100000 |

No balance claim is inferred from these numbers. The remaining verification ladder is an actual server compile/link, disposable startup test and the manual combat/lifecycle/encounter checks in `README.md`; none was run for this delivery.
