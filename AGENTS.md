# AGENTS.md - Flying BRICKS

Flying BRICKS is a Fallout 4 VR F4SE plugin (`Flying_BRICKS.dll`, with `Flying_BRICKS.esp`) that owns ballistic weapon damage and flight. Each caliber does the damage of its real cartridge on a 100-health unarmored human, flies at its real muzzle velocity with G1 drag and true gravity, and meets armor rated like real body armor. It targets Fallout 4 VR 1.2.72.0 with F4SEVR 0.6.21.

How the model works is published in `docs/`: [DamageModel.md](docs/DamageModel.md), [Ballistics.md](docs/Ballistics.md), [PatchFiles.md](docs/PatchFiles.md), and the generated tables [Calibers.md](docs/Calibers.md), [Armor.md](docs/Armor.md) and [Creatures.md](docs/Creatures.md). Read them before changing behaviour; a behaviour change updates them.

## What it changes

- Hits from owned ammunition fired by a ranged weapon take the round's damage, with armor, limb and head rules ([DamageModel.md](docs/DamageModel.md)). Melee, unarmed hits and gun bashes go through the same armor and limbs by weapon class. Explosions, energy and thrown weapons are not touched.
- At fire time the plugin selects the caliber's projectile record; at load it rewrites those records' flight in memory ([Ballistics.md](docs/Ballistics.md)).
- `Flying_BRICKS.esp` overrides the vanilla and DLC ballistic ammunition, renames the five Ballistic Weave mods to show their armor level, and adds the remaining calibers as new ammunition. It has no models of its own: its records use vanilla models, or borrow other mods' (below).
- Vanilla weapons move to the added ammunition only through optional per-weapon patches; other mods' weapons through patch files ([PatchFiles.md](docs/PatchFiles.md)).

## Layout

| Path | Contents |
| --- | --- |
| `data/RealisticDamageModel.json` | The damage formula and each cartridge's energy and diameter, with sources |
| `data/BallisticRegistry.json` | Every caliber: damage, projectile and ammo form IDs, trajectory class, names, the vanilla ammunition it takes over. `"shipped": false` marks a real caliber no supported weapon fires: it keeps its data and reserved form IDs, gets no records or runtime rows, and is listed in `Calibers.md` as mapped, not shipped |
| `data/CartridgeFlight.json` | Each cartridge's muzzle velocity and G1 ballistic coefficient, with source |
| `data/ArmorLevels.json` | Armor levels and rated energies, each caliber's stop level, armor rules, Ballistic Weave tiers, head burst and sever thresholds |
| `data/CreatureLevels.json` | Each creature race's body levels by hit zone |
| `data/Compatibility.json` | Other mods' models and flight per caliber |
| `data/WorldAmmo.json` | The game's lists that host the world ammo list |
| `data/mod/` | Game data shipped with the DLL: the ESP, the patch files and the RobCo Patcher file |
| `src/Hooks.cpp` | Every native hook: damage, armor curve, limb multiplier, removal, pellet spread, flight, fire |
| `src/Terminal.cpp` | What a round does to a target: worn armor, power armor, creatures, heads and limbs |
| `src/Melee.cpp` | Melee blows; a weapon's class comes from its unarmed form, then its `ma_*` keyword, then its animation type |
| `src/Presets.cpp` | Flight rewrite, drag and force presets |
| `src/AmmoTable.cpp`, `src/Compatibility.cpp`, `src/PatchFiles.cpp` | Owned ammunition, other mods' rounds and models, patch files |
| `src/PatchFormat.h` | The patch file format, a public contract |
| `src/DamagePolicy.h` | Which hits the model owns |
| `tools/` | Generators (below) |
| `tests/` | Policy, patch format and settings tests |

## Generated files

`data/` is the single source of every value; never edit generated files by hand.

| Generator | Writes | Rerun after |
| --- | --- | --- |
| `tools/generate_esp.py` | The Spriggit project `data/plugin`, which Spriggit compiles to `data/mod/Flying_BRICKS.esp` | Any change to the registry or armor levels |
| `tools/generate_terminal_data.py <game Data folder>` | `src/TerminalData.inl`, `src/AmmoData.inl`, `src/CompatibilityData.inl`, `src/CaliberData.inl` | Any change in `data/` |
| `tools/generate_docs.py` | `docs/Calibers.md`, `docs/Armor.md`, `docs/Creatures.md` | Any change in `data/` |

The shipped patch files in `data/mod/F4SE/Plugins/Flying_BRICKS/Patches/` are edited directly; the runtime reads them.

## Build and test

```powershell
cmake --preset custom-fast
cmake --build build-fast --config Release
cmake --build build-fast --config Release --target build_tests
ctest --test-dir build-fast -C Release --output-on-failure
```

Requires Visual Studio 2022 (v143), CMake 4.2+, vcpkg with `VCPKG_ROOT`, CommonLibF4VR, RPS UI Framework's public headers (`RPSUI_INCLUDE_DIR`) and RPS Framework built with its `custom-fast` preset (`RPSFramework_DIR`). Copy `CMakeUserPresets.json.template` to `CMakeUserPresets.json` and set `COPY_PLUGIN_BASE_PATH`: the build copies the DLL, the patch files and the RobCo Patcher file to `F4SE/Plugins` and the ESP to the root of that mod folder.

## Runtime files

- DLL and F4SE registration `Flying_BRICKS`; log `Documents/My Games/Fallout4VR/F4SE/Flying_BRICKS.log`.
- Settings `Documents/My Games/Fallout4VR/Mods_Config/Flying_BRICKS/Flying_BRICKS.toml`, read once at startup through RPS Framework's settings store: `[General] DamageModel`, `Force`, `PlayerHeadshotInstantKill`; `[Debug] HitLabels`, `HitTrace`. The first start migrates `Flying_BRICKS.ini` and deletes it. The mod has no wheel section.
- RPS UI Framework is optional at runtime and only draws the hit labels.

## Compatibility

Munitions, Armament, BIBFX and Weapons of Fate are optional and never masters; priority from low to high is that order. Boxes and casings come from Armament or Munitions (an exact caliber beats a close one), flight from Weapons of Fate, else BIBFX, else those two, and impact explosions from BIBFX except on shotguns. Their muzzle, impact and casing meshes and lights replace paths and forms our records already use, so they need no code. Numbers never come from another mod. A provider counts only when its plugin is loaded and its meshes exist.

- Their ammunition is replaced by ours for the same caliber in weapons, object mods, leveled lists, form lists, recipes and containers, in memory at GameLoaded, after RobCo Patcher's pass at GameDataReady. Records a weapon still names stay owned. Armament's RobCo file sets every ballistic weapon's base damage to 0 and gives damage only to its own rounds.
- `Flying_BRICKS.esp` loads last; an earlier position is logged and only costs fields such as names.
- Caliber Complex is incompatible: loading it logs a warning and shows a notification after each load.
- Our rounds reach vendors and loot through one world list the DLL fills with the calibers loaded weapons fire; a RobCo Patcher file generated from `data/WorldAmmo.json` adds it to the game's lists. Without RobCo Patcher the list is not reached, which is logged. Per-gun lists such as `LL_Ammo_10mm` never host it, because they also feed the NPC carrying that gun.

## Invariants

- Every callsite is verified against the live executable before the first patch. The plugin fails closed, patching nothing, when a callsite calls neither its native target nor another plugin's verified F4SE trampoline stub, or when no ammunition is owned.
- A callsite another plugin already patches keeps that patch: the hook calls through the other plugin's detour (SCISSORS patches the actor-hit call).
- Ownership comes from the generated tables and the patch files, by form, never from a record's live values or winning plugin. Damage is read from the table, so no plugin or patcher changes it; ammunition outside the table keeps native damage, never zero.
- Plugin forms resolve through `src/PluginForms.h`, because CommonLib's lookup is blind to light plugins on this stack.
- A caliber's suppressed projectile has the normal projectile's form ID plus `0x100`.
- The flight rewrite touches only `Flying_BRICKS.esp`'s own projectile records, in memory.
