# Flying BRICKS

Realistic ballistics for Fallout 4 VR. Every caliber does the damage its real
cartridge would, flies at its real speed with air drag and gravity, and meets
armor rated like real body armor.

## Features

- **Damage by caliber.** Conventional ammunition deals damage from an authored
  model of each cartridge (muzzle energy and bullet diameter, scaled to a
  100 HP unarmored human), the same whoever fires it and on every difficulty.
  Weapon mods, perks, criticals and difficulty no longer scale it; blocking
  still works. A round loses damage over distance as air drag takes its energy.
- **Armor levels.** On humans, ghouls, synths and super mutants, each worn
  armor piece has a level from 1 to 4, compared with NIJ ratings, and each
  caliber has the level that stops it. Head hits, limb hits, head explosions and
  limb removal follow the caliber. Clothing and hats are not armor. The five
  Ballistic Weave tiers are renamed to show the armor level they give.
- **Creatures** read their own body as armor by hit zone: a deathclaw's chest
  is not its head.
- **Power armor** is its own rule per piece, by each projectile's energy: up to
  1090 ft-lb nothing reaches the wearer, below 11000 ft-lb a small share scaled
  by energy does, and only anti-materiel rounds go through. Every round wears the piece down by its
  energy until the piece comes off, and an exposed limb takes the round like
  bare skin.
- **Real flight.** Each caliber flies at its own muzzle velocity with G1 air
  drag and true gravity. Pellet weapons fire a real shotgun cone.
- **Melee**, unarmed hits and gun bashes from anyone go through the same limb
  and armor rules: the weapon's class decides how much of a blow crosses each
  armor level and suit piece, and how much it wears the piece.
- **New calibers** are added as ammunition items, and our rounds reach vendors
  and loot for every caliber a loaded weapon fires (needs RobCo Patcher).
- Explosions, energy weapons and thrown weapons are not changed.

## How it works

The research behind the numbers, with sources, and the full tables:

- [Damage model](docs/DamageModel.md): the damage formula, range, armor, heads and limbs, power armor, creatures and melee.
- [Ballistics](docs/Ballistics.md): flight, air drag, gravity, force presets and the shotgun cone.
- [Calibers](docs/Calibers.md): every caliber's energy, damage at range, stop level, velocity and sources.
- [Armor](docs/Armor.md): armor levels, the rules that give each armor its level, and Ballistic Weave.
- [Creatures](docs/Creatures.md): each creature's body levels by hit zone.
- [Patch files](docs/PatchFiles.md): putting another mod's weapons on Flying BRICKS ammunition.

## Requirements

- Fallout 4 VR 1.2.72.0 and F4SEVR 0.6.21.
- `Flying_BRICKS.esp`, included, active and loaded last (after Armament, BIBFX
  and Weapons of Fate when you use them). An earlier position only costs a few
  fields such as names, and is logged.
- Recommended: RobCo Patcher (VR port), so our rounds reach vendors and loot.
- Optional: RPS UI Framework, only for the hit labels.

## Compatibility

- **Munitions, Armament, BIBFX and Weapons of Fate** are supported, all
  optional. Their ammunition is replaced by ours for the same caliber
  everywhere (weapons, weapon mods, leveled lists, recipes, containers), and our
  rounds borrow their models: boxes and casings from Armament or Munitions,
  bullets from Weapons of Fate, BIBFX or those two, impact effects from BIBFX.
  Without them our rounds use vanilla models. The numbers always stay ours.
- **Caliber Complex is not compatible.** Its rounds are not replaced and its
  lists flood vendors and loot; with it loaded you get a warning after each
  load.
- **Weapons from other mods** are covered by the patch files shipped in
  `F4SE/Plugins/Flying_BRICKS/Patches/`, one per mod, for several hundred weapon
  mods. A patch is only read when that mod is loaded. Anyone can write one; see
  [Patch files](docs/PatchFiles.md). A weapon on a round Flying BRICKS
  does not own (spikes, 2mm, nails, arrows) keeps its own damage.
- **SCISSORS** uses Flying BRICKS' per-caliber forces for the push a bullet
  gives a struck body, and its physical melee scale multiplies blows.

## Settings

`Documents\My Games\Fallout4VR\Mods_Config\Flying_BRICKS\Flying_BRICKS.toml`,
read once when the game starts. The first start creates it with every option at
its default and its help above it; an older `Flying_BRICKS.ini` is carried over
and deleted.

| Setting | Default | Purpose |
| --- | --- | --- |
| `[General] DamageModel` | `true` | The per-caliber damage, armor and limb model. `false` keeps the game's damage. |
| `[General] Force` | `0` | How hard rounds push what they hit: 0 Weapons of Fate, 1 three times the bullet's momentum, 2 vanilla. |
| `[General] PlayerHeadshotInstantKill` | `true` | A head hit that gets through kills you, as it kills a human NPC. `false` gives double head damage instead. |
| `[Debug] HitLabels` | `false` | A marker and a label at each recent hit (needs RPS UI Framework). |
| `[Debug] HitTrace` | `false` | Logs each hit's damage calculation. |

The log is `Documents\My Games\Fallout4VR\F4SE\Flying_BRICKS.log`.

<details>
<summary>Building from source</summary>

Requires Windows x64, Visual Studio 2022 with the v143 C++ toolset, CMake 4.2 or
newer, vcpkg with `VCPKG_ROOT` set, CommonLibF4VR, RPS UI Framework's public
headers (`RPSUI_INCLUDE_DIR`) and RPS Framework built with its `custom-fast`
preset (`RPSFramework_DIR`). Copy `CMakeUserPresets.json.template` to
`CMakeUserPresets.json` and set `COPY_PLUGIN_BASE_PATH` to your mod folder.

```powershell
cmake --preset custom-fast
cmake --build build-fast --config Release

cmake --build build-fast --config Release --target build_tests
ctest --test-dir build-fast -C Release --output-on-failure
```

`data/` holds the damage model and ballistic registry; `tools/generate_esp.py`
writes the plugin source from it, `tools/generate_terminal_data.py` the
generated runtime tables and `tools/generate_docs.py` the tables in `docs/`.
Never edit generated files by hand.

</details>

## License

[GNU GPL v3 only (GPL-3.0-only)](LICENSE).
