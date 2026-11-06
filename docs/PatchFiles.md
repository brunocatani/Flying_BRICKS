# Patch files

A patch file puts the weapons of another mod on Flying BRICKS ammunition, without a plugin and without rebuilding
anything. Anyone can write one. Flying BRICKS ships patch files for several hundred weapon mods.

## Where they go

`Data/F4SE/Plugins/Flying_BRICKS/Patches/<plugin file name>.toml`, one file per plugin, named after it, for example
`Patches/MyWeaponMod.esp.toml`.

- A file is read only when a version of its plugin is loaded. Every file for a plugin's base name applies to its loaded
  `.esp`, `.esl` or `.esm`, and the file named exactly after the loaded file applies last.
- A version of the plugin with different form IDs, such as a compacted one, needs rows with those IDs in the same file.
- A mod that loads later replaces a file of the same name, so a weapon mod can ship its own patch.
- Patches apply at game load, after RobCo Patcher's pass, so what they set wins over RobCo Patcher files.

## Format

TOML, with one array of tables per kind of row:

```toml
[[weapon]]
id = "000F9B"
caliber = "9x19"
name = "My Pistol"

[[part]]
id = "0012AB"
ammo = "Fallout4.esm|01F276"

[[round]]
id = "000FA8"
caliber = "9x19"

[[damage]]
id = "000805"
damage = 36

[[default]]
id = "000F99"
from = "MyWeaponMod.esp|003607"
to = "MyWeaponMod.esp|003606"
```

| Row | Effect |
| --- | --- |
| `[[weapon]]` | The weapon `id` fires this caliber (`caliber`) or this exact ammunition record (`ammo`), whatever another mod left on it |
| `[[part]]` | The same for an object mod that changes the ammunition |
| `[[round]]` | The plugin's own ammunition `id` becomes Flying BRICKS' round of that caliber everywhere: weapons, object mods, leveled lists, form lists, recipes and containers |
| `[[damage]]` | The weapon `id`, on a round Flying BRICKS does not own (spikes, 2mm, nails, arrows), gets this base damage back if another mod set it to 0. A whole number from 1 to 65535 |
| `[[default]]` | The weapon `id` spawns with object mod `to` instead of `from` on the same attach point, for a gun whose default part is on the wrong round |

- `id` is the record's hex form ID in quotes, from the plugin the file is named after. A load order prefix is
  dropped, so an ID copied from the game console or an editor works, including a light plugin's `FE` form.
- `caliber` is a caliber ID from the main table of [Calibers](Calibers.md), such as `9x19` or `556x45`. The IDs
  listed there as mapped, not shipped have no ammunition in the mod and cannot be used.
- `ammo`, `from` and `to` name a record in any plugin as `"Plugin.esp|01F276"`.
- `name` is an optional label on any row.
- `[[weapon]]` and `[[part]]` take either `caliber` or `ammo`, never both.

A bad row is reported in `Flying_BRICKS.log` and skipped; the other rows of the file still apply. An unknown section
is reported too.

## Damage

A patched weapon does the damage of its caliber ([Damage model](DamageModel.md)); a patch never sets a damage number
for Flying BRICKS ammunition. Each caliber has an NPC ammo list that patched weapons use, so NPCs carrying them get the
right rounds.
