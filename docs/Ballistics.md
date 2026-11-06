# Ballistics

How Flying BRICKS rounds fly. Each caliber's muzzle velocity and ballistic coefficient are in [Calibers](Calibers.md).

## Flight

At load the plugin rewrites `Flying_BRICKS.esp`'s own projectile records in memory; nothing is saved to the game's
save file, and no other mod's projectiles are touched.

- **Speed**: each caliber leaves the muzzle at its real muzzle velocity, at 70 game units per metre.
- **Drag**: every step of flight slows the round by
  `pi / 8 x air density (1.225 kg/m3) x G1 drag x speed squared / (BC x 703.07)`, in SI units, where G1 drag is the
  standard G1 drag function at the round's Mach number (speed of sound 340 m/s) and BC its G1 ballistic coefficient.
  A round with a high coefficient keeps its speed; a pistol bullet or a shotgun pellet loses it quickly.
- **Gravity**: real gravity, so a round drops as a real one does at that speed.
- **Range and lifetime** are long enough never to cut a shot short.

Drag changes speed and drop, and through the energy left at impact it changes damage ([Damage model](DamageModel.md#over-distance)).

## Muzzle velocity and ballistic coefficient

Values come from the Wikipedia table of handgun and rifle cartridges where a row matches the caliber's energy and
diameter. Where that row's top speed implies a bullet too light for the caliber, because its speed and energy come
from different loads, the speed is the one a plausible bullet needs for that energy ("table corrected"). Shotguns use
a buckshot pellet's speed and coefficient. A coefficient the table lacks is computed from the implied bullet mass,
diameter and a form factor. Each caliber's row in [Calibers](Calibers.md) names its source.

## Force

`[General] Force` sets how hard a round pushes what it hits:

| Value | Preset | Push |
| --- | --- | --- |
| 0 (default) | Weapons of Fate | The forces of the Weapons of Fate mod |
| 1 | Momentum | Three times the bullet's momentum in kg m/s, per pellet for shotguns, which puts .308 at the Weapons of Fate value |
| 2 | Vanilla | The base game's forces |

Force is set per trajectory class, not per caliber:

| Class | Weapons of Fate | Momentum | Vanilla |
| --- | --- | --- | --- |
| .38 Special | 12 | 6 | 8 |
| .45 ACP | 23 | 11 | 8 |
| 10mm | 16 | 14 | 8 |
| .44 Magnum | 20 | 19 | 8 |
| .45-70 | 25 | 31 | 8 |
| 12 gauge buckshot | 10 | 5 | 4 |
| 4.73mm caseless | 10 | 10 | 3 |
| 5.56mm | 16 | 11 | 7 |
| 7.62x39mm | 20 | 17 | 7 |
| .308 | 25 | 25 | 7 |
| .50 BMG | 35 | 112 | 7 |

## Shotgun cone

Pellet weapons firing Flying BRICKS ammunition spread their pellets in a cone with a half angle of 0.0104 radians:
half of a 0.75 inch per yard pattern, between the 0.5 inch of modern stock barrels and the older 1 inch rule of thumb.
