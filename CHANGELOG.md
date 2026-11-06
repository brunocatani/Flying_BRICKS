# Changelog

## Unreleased

- 77 calibers no supported weapon fires are no longer in the plugin. The real
  ones are listed in `docs/Calibers.md` as mapped but not shipped; 13 fictional
  or duplicate rounds are gone. A patch file row naming one of them is logged
  and skipped.

## 1.0.0

First release.

- Per-caliber damage for conventional ammunition, independent of weapon mods,
  perks, criticals and difficulty, with blocking kept. Damage and armor
  penetration follow the energy a round keeps over the distance it flew.
- Armor levels 1 to 4 on worn armor, compared with NIJ ratings; head and limb
  rules, head explosions and limb removal by caliber. Clothing and hats are not
  armor; hand hits count as the arm. Ballistic Weave tiers show the armor level
  they give.
- Creatures read their own body as armor by hit zone, with anatomy-based limb
  multipliers in place of vanilla values that contradicted it.
- Power armor rule per piece by projectile energy, with piece wear.
- Real-world flight for every caliber: its own muzzle velocity, G1 air drag
  and true gravity. Shotguns fire a real pellet cone.
- Locational melee damage through the same limb and armor rules, by weapon
  class.
- New calibers as ammunition items, and a world ammunition list that brings our
  rounds to vendors and loot for the calibers loaded weapons fire (through
  RobCo Patcher).
- Compatibility with Munitions, Armament, BIBFX and Weapons of Fate: their
  ammunition is replaced by ours and their models are borrowed. Caliber Complex
  is incompatible and gives a warning after each load.
- Patch files for several hundred weapon mods in
  `F4SE/Plugins/Flying_BRICKS/Patches/`, one per mod, read only when that mod
  is loaded. Anyone can add one (format in `src/PatchFormat.h`).
- Settings in `Flying_BRICKS.toml` (`Mods_Config\Flying_BRICKS\`): damage model,
  force preset, player head shots, hit labels and hit log.
- Hooks chain behind another plugin's patch of the same game code (SCISSORS and
  others) instead of refusing.
- RobCo PALM's Item Spawner lists every Flying BRICKS round under All ammo.
