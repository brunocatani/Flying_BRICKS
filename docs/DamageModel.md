# Damage model

How Flying BRICKS decides what a round does when it hits. The per-caliber numbers are in [Calibers](Calibers.md),
the armor levels in [Armor](Armor.md) and the creature table in [Creatures](Creatures.md).

## Which hits it decides

A hit belongs to the model when it is a round of ammunition Flying BRICKS owns ([Calibers](Calibers.md)) fired from a
ranged weapon, and it is not an explosion. For those hits the round's own damage replaces the weapon's, and weapon
mods, perks, criticals, sneak attacks, blocking and difficulty no longer scale it. Everything else keeps the game's
damage: energy weapons, explosions, thrown weapons, and ammunition from other mods that no patch file hands over.
Melee blows have their own rule ([below](#melee)).

`[General] DamageModel = false` in `Flying_BRICKS.toml` turns the whole model off.

## Damage at the muzzle

Each caliber's damage is fixed by its muzzle energy and bullet diameter, on a scale where a human with no armor has
100 health and a .308 round of 1200 ft-lb is the reference:

```text
damage = round(100 x (energy / 1200)^0.6 x (diameter / 0.308)^0.3), at least 1
```

So a 9x19mm round (342 ft-lb, 0.355 in) does 49, 5.56 NATO 91, .308 174 and .50 BMG 490. Energy weighs most, and a
wider bullet does somewhat more for the same energy. Muzzle energy is a rough indicator of severity, not a full
wound model: bullet construction, barrel length and shot placement are outside it.

## Over distance

A round loses speed to air drag as it flies ([Ballistics](Ballistics.md)), and its energy falls with the square of its
speed. A hit keeps `(energy at impact / muzzle energy)^0.6` of its damage, the same exponent as the formula, so damage
follows the energy that arrives. The distance is from the shooter to the impact. The round's class does not change
with distance: whether armor stops it, whether it crosses a power armor piece and whether a head can burst are decided
by its muzzle energy, while the amounts (damage, what survives armor, how much a piece is worn) use the energy at
impact.

## Who reads armor

| Target | Rule |
| --- | --- |
| Humans (gen 3 synths use the human race), ghouls and super mutants | Worn armor, per hit limb |
| Anyone in power armor | The suit piece on the hit limb |
| Creatures in [Creatures](Creatures.md) | Their own body as armor, per hit zone |
| Any other race | The game's own armor curve |

## Worn armor

The armor level of a limb is the highest level among the worn items covering it; a helmet is never above level 2.
The round is **stopped** when that level is at or above the caliber's stop level.

- **Stopped**: 25% of the damage gets through a helmet, 10% through soft armor (levels 1 and 2) and 5% through hard
  armor (levels 3 and 4). A stopped round never kills outright and never removes a head or limb.
- **Not stopped**: a rifle round (stop level 3 or above) through soft armor keeps 90% of its damage. Otherwise it keeps
  `((energy - rated energy of the armor level) / energy)^0.6`, never less than 25%. No armor keeps all of it.

Then the hit limb scales it: arms x0.5, legs x0.6, torso x1. A head hit that is not stopped kills humans, ghouls and anyone
in power armor outright; others, such as super mutants, take x2. `[General] PlayerHeadshotInstantKill = false` gives the player x2 instead of the kill.

A round the armor fully holds still does 0.001 damage, because the game plays a hit's impact effect and reaction only
for damage above zero.

## Heads and limbs

Only a killing hit removes anything, and only the limb it struck. A head bursts when the round's energy and the range
allow it ([Armor](Armor.md#heads-and-limbs)); a limb is severed when the round carries enough energy. The body part
decides how it comes off: a head that cannot burst is severed instead, and a limb that cannot be severed is shot off.

## Power armor

Each suit piece is judged on its own, by the energy of one projectile (a shotgun pellet carries its share of the shot):

| Energy per projectile | Result |
| --- | --- |
| Up to 1090 ft-lb | Nothing reaches the wearer |
| Above 1090, below 11000 ft-lb | Stopped; the wearer takes `energy / 120000` of the damage (5.56 NATO about 1%) |
| 11000 ft-lb and above (.50 BMG, 12.7x108mm, 13.2mm TuF and heavier) | Goes through, keeping `((energy - piece rating x 30) / energy)^0.6` of its damage, at least 25%; a head hit kills |

Every round also wears the piece by `(energy / 1196)^2` points: one point for a 5.56 NATO round, about 6 for .308 and
122 for .50 BMG, against 50 to 560 health on a piece. A fraction of a point is a chance of one. The game keeps the
piece's health, applies its NPC multiplier and takes the piece off at zero; the exposed limb then takes rounds like
bare skin.

## Creatures

A creature's hit zone has a level on the same scale, and a round is stopped the same way as by worn armor: 10% gets
through soft zones and 5% through hard ones, and a round that gets through keeps the same share. Each zone can carry
its own damage factor, and a race either dies to a head hit that gets through or not ([Creatures](Creatures.md)).

## Shotguns

Pellet weapons firing Flying BRICKS ammunition use a real shotgun cone: a half angle of 0.0104 radians, half of a
0.75 inch per yard pattern. The game divides the shot's damage among the pellets.

## Melee

Melee hits, unarmed hits and gun bashes from any attacker go through the same limbs and armor. A weapon a player can
carry does a fixed amount on the same 100-health scale; creatures, robots and other mods' weapons keep the game's
number and take only the armor rule. Weapon damage mods still add to the fixed amount, blocking works as in the game,
and SCISSORS' physical melee scale multiplies the blow.

| Weapon | Damage | Class |
| --- | --- | --- |
| Boxing glove | 8 | Fist |
| Fists | 12 | Fist |
| Ripper | 12 | Saw |
| Knuckles | 18 | Fist |
| Light blunt weapons | 20 | Fist |
| Switchblade | 30 | Point |
| Knife | 35 | Point |
| Clubs | 35 | Blunt |
| Gun bash | 25 | Blunt |
| Power armor fist | 40 | Crush |
| Power fist | 45 | Crush |
| Shishkebab | 45 | Edge |
| Machete | 50 | Edge |
| Deathclaw gauntlet | 55 | Claw |
| Sword | 60 | Edge |
| Sledgehammer | 70 | Crush |
| Super sledge | 100 | Crush |

The class sets how much of a blow gets through each worn armor level, and behind an intact power armor piece:

| Class | Level 0 | 1 | 2 | 3 | 4 | Power armor |
| --- | --- | --- | --- | --- | --- | --- |
| Point | 100% | 60% | 30% | 8% | 5% | 0% |
| Edge | 100% | 45% | 20% | 8% | 5% | 0% |
| Heavy edge | 100% | 55% | 30% | 12% | 8% | 1% |
| Saw | 100% | 60% | 35% | 10% | 6% | 1% |
| Fist | 100% | 70% | 50% | 25% | 15% | 0% |
| Blunt | 100% | 85% | 65% | 40% | 30% | 1% |
| Crush | 100% | 90% | 75% | 55% | 45% | 2% |
| Claw | 100% | 70% | 50% | 20% | 12% | 3% |

An edge is defeated by anything hard and goes through cloth, while force crosses plate. A bare head takes a blow x2,
a helmeted one x1. No blow kills outright.

These percentages and the power armor values are tuning, chosen for the shape real armor shows; no source converts
them to health.
