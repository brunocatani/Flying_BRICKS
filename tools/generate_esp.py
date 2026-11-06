"""Writes the Spriggit project for Flying_BRICKS.esp from data/BallisticRegistry.json.

Output goes to data/plugin. Compile it to data/mod/Flying_BRICKS.esp with Spriggit. Never edit the
output by hand: the registry is the single source.

Model paths are vanilla placeholders until the mod's own assets exist.
"""
import json
import pathlib
import re
import shutil

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "data" / "plugin"
PLUGIN = "Flying_BRICKS.esp"

FLAG_NAMES = {1: "IgnoresNormalWeaponResistance", 2: "NonPlayable", 4: "HasCountBased3d"}
AMMO_KEYWORD = "0F4AE8:Fallout4.esm"
PREVIEW_TRANSFORM = "07B9C1:Fallout4.esm"
PICK_UP_SOUND = "0347DF:Fallout4.esm"
PUT_DOWN_SOUND = "0347E0:Fallout4.esm"
SOUND_LEVEL_NORMAL = 1
SOUND_LEVEL_SUPPRESSED = 4
AMMO_LIST_OFFSET = 0x300  # A caliber's NPC ammo list is its normal projectile's local ID plus this.
WORLD_AMMO_LIST = "000BA0"  # Above the caliber lists. Also in generate_terminal_data.py.

# Vanilla placeholders per trajectory class: flight model, ammo box, casing.
PLACEHOLDERS = {
    "wof_10mm": (r"Ammo\10mm\Projectile10mm.nif", r"Ammo\10mm\10mmAmmo.nif", r"Ammo\10mm\10mmShellCasing.nif"),
    "wof_38": (r"Ammo\38\Projectile38.nif", r"Ammo\38\38Ammo.nif", r"Ammo\38\38Casing.nif"),
    "wof_44": (r"Ammo\45Cal\Projectile45cal.nif", r"Ammo\44\44Ammo.nif", r"Ammo\44\44Casing.nif"),
    "wof_45": (r"Ammo\45Cal\Projectile45cal.nif", r"Ammo\45Cal\45Ammo.nif", r"Ammo\45Cal\45Casing.nif"),
    "wof_4570": (r"Ammo\45Cal\Projectile45cal.nif", r"DLC03\Ammo\4570Ammo.nif", r"DLC03\Ammo\4570Casing.nif"),
    "wof_308": (r"Ammo\308\Projectile308cal.nif", r"Ammo\308\308Ammo.nif", r"Weapons\HuntingRifle\Casing308.nif"),
    "wof_50": (r"Ammo\308\Projectile308cal.nif", r"Ammo\50Cal\50CalAmmo.nif", r"Ammo\50Cal\50CalCasing.nif"),
    "wof_556": (r"Ammo\308\Projectile308cal.nif", r"Ammo\556\556Ammo.nif", r"Ammo\556\556Casing.nif"),
    "wof_5mm": (r"Ammo\308\Projectile308cal.nif", r"Ammo\5mm\5mmAmmo.nif", r"Ammo\5mm\5mmCasing.nif"),
    "wof_762": (r"Ammo\308\Projectile308cal.nif", r"DLC04\Ammo\762Ammo.nif", r"DLC04\Weapons\RaiderGuns\762Casing.nif"),
    "wof_shotgun": (r"Ammo\ShotgunShell\ProjectileShotPellet.nif", r"Ammo\ShotgunShell\ShotgunShellAmmo.nif", r"Ammo\ShotgunShell\SpentShotgunShell.nif"),
}


def number(value):
    return f"{value:.10g}"


def quoted(text):
    return "'" + text.replace("'", "''") + "'"


def safe_id(text):
    return re.sub(r"[^A-Za-z0-9]", "_", text)


def check_damage(registry, model):
    """Every registry damage value must still be what the published model computes."""
    scale = model["model"]
    cartridges = {row["id"]: row for row in model["cartridges"]}
    for row in registry["projectiles"]:
        cartridge = cartridges[row["id"]]
        expected = scale["humanReferenceHealth"] * (cartridge["energyFtLb"] / scale["referenceEnergyFtLb"]) ** scale["energyExponent"] * (
            cartridge["diameterIn"] / scale["referenceDiameterIn"]
        ) ** scale["diameterExponent"]
        expected = max(scale["minimumDamage"], int(expected + 0.5))
        if expected != row["damage"]:
            raise SystemExit(f"{row['id']}: registry damage {row['damage']} but the model computes {expected}")


def projectile_yaml(editor_id, local_id, profile, trajectory, presentation, placeholder, suppressed):
    classes = presentation["classes"][profile["presentationClass"]]
    light = presentation["suppressedMuzzleLight" if suppressed else "normalMuzzleLight"]
    muzzle = classes["suppressedMuzzleModel" if suppressed else "normalMuzzleModel"]
    return "\n".join([
        f"FormKey: {local_id}:{PLUGIN}",
        f"EditorID: {editor_id}",
        "Model:",
        f"  File: {placeholder}",
        "Unused: '[]'",
        "Flags:",
        "- MuzzleFlash",
        "- PassThroughSmallTransparent",
        "- PenetratesGeometry",
        "Type: Missile",
        f"Gravity: {number(trajectory['gravity'])}",
        f"Speed: {number(trajectory['speed'])}",
        f"Range: {number(trajectory['range'])}",
        f"MuzzleFlash: {light}",
        f"Sound: {presentation['sound']}",
        f"MuzzleFlashDuration: {number(classes['muzzleFlashDuration'])}",
        f"FadeDuration: {number(classes['fadeDuration'])}",
        f"ImpactForce: {number(trajectory['impactForce'])}",
        f"CollisionRadius: {number(trajectory['collisionRadius'])}",
        f"Lifetime: {number(trajectory['lifetime'])}",
        f"RelaunchInterval: {number(trajectory['relaunchInterval'])}",
        f"CollisionLayer: {presentation['collisionLayer']}",
        f"TracerFrequency: {trajectory['tracerFrequency']}",
        f"MuzzleFlashModel: {muzzle}",
        f"SoundLevel: {SOUND_LEVEL_SUPPRESSED if suppressed else SOUND_LEVEL_NORMAL}",
        "",
    ])


def ammo_yaml(item):
    """item: form key, editor ID, bounds, item fields, models, flags, damage and projectile."""
    lines = [f"FormKey: {item['formKey']}"]
    if item.get("notPlayable"):
        lines.append("MajorRecordFlagsRaw: 4")
    lines.append(f"EditorID: {item['editorID']}")
    if item.get("notPlayable"):
        lines += ["Fallout4MajorRecordFlags:", "- NotPlayable"]
    bounds = item["objectBounds"]
    lines += [
        "ObjectBounds:",
        f"  First: {bounds[0]}, {bounds[1]}, {bounds[2]}",
        f"  Second: {bounds[3]}, {bounds[4]}, {bounds[5]}",
        f"PreviewTransform: {item['previewTransform']}",
        "Name:",
        "  TargetLanguage: English",
        f"  Value: {quoted(item['name'])}",
        "Model:",
        f"  File: {item['model']}",
    ]
    if item.get("modelData"):
        lines.append(f"  Data: 0x{item['modelData']}")
    lines += [
        f"PickUpSound: {PICK_UP_SOUND}",
        f"PutDownSound: {PUT_DOWN_SOUND}",
        "Description:",
        "  TargetLanguage: English",
        "  Value: ''",
        "Keywords:",
        f"- {AMMO_KEYWORD}",
        f"Value: {item['value']}",
        f"Weight: {number(item['weight'])}",
        f"Projectile: {item['projectile']}",
        "Flags:",
    ]
    lines += [f"- {name}" for bit, name in FLAG_NAMES.items() if item["flags"] & bit]
    lines += [
        f"Damage: {number(item['damage'])}",
        "ShortName:",
        "  TargetLanguage: English",
        f"  Value: {quoted(item['shortName'])}",
        f"CasingModel: {item['casingModel']}",
    ]
    if item.get("casingModelData"):
        lines.append(f"ModelInfo: 0x{item['casingModelData']}")
    if item.get("notPlayable"):
        lines += ["MajorFlags:", "- NonPlayable"]
    lines.append("")
    return "\n".join(lines)


def weave_yaml(row):
    """Vanilla's Ballistic Weave record with its name and description showing the armor level."""
    return "\n".join([
        "MutagenObjectType: ArmorModification",
        f"FormKey: {row['localFormID']}:Fallout4.esm",
        f"EditorID: {row['editorID']}",
        "Version2: 5",
        "Name:",
        "  TargetLanguage: English",
        f"  Value: {quoted(row['name'])}",
        "Description:",
        "  TargetLanguage: English",
        f"  Value: {quoted(row['description'])}",
        "AttachPoint: 20DE3E:Fallout4.esm",
        "TargetOmodKeywords:",
        "- 20DE40:Fallout4.esm",
        f"LooseMod: {row['looseMod']}:Fallout4.esm",
        "Properties:",
        "- MutagenObjectType: ObjectModIntProperty<Armor+Property>",
        "  Property: Rating",
        f"  Value: {row['rating']}",
        "  FunctionType: Add",
        "- MutagenObjectType: ObjectModIntProperty<Armor+Property>",
        "  Property: Value",
        f"  Value: {row['value']}",
        "  FunctionType: Add",
        "- MutagenObjectType: ObjectModFormLinkIntProperty<Armor+Property>",
        "  Property: Keywords",
        "  Record: 20DE45:Fallout4.esm",
        "  Value: 1",
        "  FunctionType: Add",
        "- MutagenObjectType: ObjectModFormLinkFloatProperty<Armor+Property>",
        "  Property: DamageTypeValue",
        "  Record: 060A81:Fallout4.esm",
        f"  Value: {row['energy']}",
        "  FunctionType: Add",
        "",
    ])


def ammo_list_yaml(editor_id, local_id, reference):
    # Vanilla's LL_Ammo_Deliverer holds the same five counts of one round.
    lines = [
        f"FormKey: {local_id}:{PLUGIN}",
        f"EditorID: {editor_id}",
        "MaxCount: 0",
        "Flags:",
        "- CalculateFromAllLevelsLessThanOrEqualPlayer",
        "- CalculateForEachItemInCount",
        "Entries:",
    ]
    for count in range(5, 10):
        lines += ["- Data:", "    Level: 1", f"    Reference: {reference}", f"    Count: {count}"]
    return "\n".join(lines) + "\n"


def write_world_ammo(list_folder, first_list):
    # The slots are placeholders: the plugin rewrites them at load with the calibers loaded guns fire. baseListCount is
    # signed in the engine's header, so the list stays at 127 entries.
    editor_id = "FlyingBricks_AmmoList_World"
    lines = [
        f"FormKey: {WORLD_AMMO_LIST}:{PLUGIN}",
        f"EditorID: {editor_id}",
        "MaxCount: 0",
        "Flags:",
        "- CalculateFromAllLevelsLessThanOrEqualPlayer",
        "- CalculateForEachItemInCount",
        "Entries:",
    ]
    for _ in range(127):
        lines += ["- Data:", "    Level: 1", f"    Reference: {first_list}:{PLUGIN}", "    Count: 1"]
    write(list_folder, f"{editor_id} - {WORLD_AMMO_LIST}_{PLUGIN}.yaml", "\n".join(lines) + "\n")

    hosts = json.loads((ROOT / "data" / "WorldAmmo.json").read_text(encoding="utf-8"))["hosts"]
    rules = ["// Generated by tools/generate_esp.py from data/WorldAmmo.json. Do not edit."]
    for host in hosts:
        added = ",".join([f"{PLUGIN}|{WORLD_AMMO_LIST.lstrip('0')}~1~{host['count']}~0"] * host["entries"])
        rules.append(f"filterByLLs={host['plugin']}|{host['id']}:addToLLs={added}")
    folder = ROOT / "data" / "mod" / "F4SE" / "Plugins" / "RobCo_Patcher" / "leveledList"
    folder.mkdir(parents=True, exist_ok=True)
    write(folder, f"{PLUGIN}.ini", "\n".join(rules) + "\n")


def write(folder, name, text):
    (folder / name).write_text(text, encoding="utf-8", newline="\n")


def shipped(registry):
    """The calibers the mod ships. A caliber with "shipped": false is research data only: no weapon uses it, so it gets
    no records, and its form IDs stay reserved for it in case one does."""
    return [row for row in registry["projectiles"] if row.get("shipped", True)]


def main():
    registry = json.loads((ROOT / "data" / "BallisticRegistry.json").read_text(encoding="utf-8"))
    model = json.loads((ROOT / "data" / "RealisticDamageModel.json").read_text(encoding="utf-8"))
    check_damage(registry, model)

    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    projectile_folder = OUTPUT / "Projectiles"
    ammo_folder = OUTPUT / "Ammunitions"
    projectile_folder.mkdir(parents=True)
    ammo_folder.mkdir()

    write(OUTPUT, "spriggit-meta.json", json.dumps({
        "PackageName": "Spriggit.Yaml.Fallout4",
        "Version": "0.40",
        "Release": "Fallout4VR",
        "ModKey": PLUGIN,
    }, indent=2) + "\n")
    write(OUTPUT, "RecordData.yaml", "\n".join([
        f"ModKey: {PLUGIN}",
        "GameRelease: Fallout4VR",
        "ModHeader:",
        "  Stats:",
        "    Version: 0.95",
        "  Author: brunocatani",
        "  Description: Flying BRICKS ballistic ammunition and projectiles",
        "  MasterReferences:",
        "  - Master: Fallout4.esm",
        "    FileSize: 0",
        "  - Master: DLCCoast.esm",
        "    FileSize: 0",
        "  - Master: DLCNukaWorld.esm",
        "    FileSize: 0",
        "  INTV: 0x01000000",
        "",
    ]))

    trajectories = {row["id"]: row for row in registry["trajectoryProfiles"]}
    calibers = shipped(registry)
    profiles = {row["id"]: row for row in calibers}
    presentation = registry["presentation"]
    for profile in calibers:
        trajectory = trajectories[profile["trajectory"]]
        placeholder = PLACEHOLDERS[profile["trajectory"]]
        for suppressed, key, suffix in ((False, "normalLocalFormID", ""), (True, "suppressedLocalFormID", "_Suppressed")):
            editor_id = f"FlyingBricks_Projectile_{safe_id(profile['id'])}{suffix}"
            local_id = profile[key]
            write(projectile_folder, f"{editor_id} - {local_id}_{PLUGIN}.yaml", projectile_yaml(
                editor_id, local_id, profile, trajectory, presentation, placeholder[0], suppressed))

        if "ammoLocalFormID" in profile:
            editor_id = f"FlyingBricks_Ammo_{safe_id(profile['id'])}"
            write(ammo_folder, f"{editor_id} - {profile['ammoLocalFormID']}_{PLUGIN}.yaml", ammo_yaml({
                "formKey": f"{profile['ammoLocalFormID']}:{PLUGIN}",
                "editorID": editor_id,
                "objectBounds": [0, 0, 0, 0, 0, 3],
                "previewTransform": PREVIEW_TRANSFORM,
                "name": profile["name"],
                "shortName": profile["shortName"],
                "model": placeholder[1],
                "casingModel": placeholder[2],
                "value": profile["value"],
                "weight": profile["weight"],
                "flags": 4,
                "damage": profile["damage"],
                "projectile": f"{profile['normalLocalFormID']}:{PLUGIN}",
            }))

    for row in registry["vanillaAmmo"]:
        profile = profiles[row["profile"]]
        item = dict(row)
        item["formKey"] = f"{row['localFormID']}:{row['plugin']}"
        item["previewTransform"] = f"{row['previewTransform']}:Fallout4.esm"
        item["damage"] = profile["damage"]
        item["projectile"] = f"{profile['normalLocalFormID']}:{PLUGIN}"
        write(ammo_folder, f"{row['editorID']} - {row['localFormID']}_{row['plugin']}.yaml", ammo_yaml(item))

    # One NPC ammo list per caliber, for weapons the patch table moves to a round their own list does not hold.
    ours = {}
    for profile in calibers:
        if "ammoLocalFormID" in profile:
            ours[profile["id"]] = f"{profile['ammoLocalFormID']}:{PLUGIN}"
    for row in registry["vanillaAmmo"]:
        if not row["notPlayable"] and row["profile"] not in ours:
            ours[row["profile"]] = f"{row['localFormID']}:{row['plugin']}"
    list_folder = OUTPUT / "LeveledItems"
    list_folder.mkdir()
    for profile in calibers:
        local_id = f"{int(profile['normalLocalFormID'], 16) + AMMO_LIST_OFFSET:06X}"
        editor_id = f"FlyingBricks_AmmoList_{safe_id(profile['id'])}"
        write(list_folder, f"{editor_id} - {local_id}_{PLUGIN}.yaml", ammo_list_yaml(editor_id, local_id, ours[profile["id"]]))
    write_world_ammo(list_folder, f"{int(calibers[0]['normalLocalFormID'], 16) + AMMO_LIST_OFFSET:06X}")

    levels = json.loads((ROOT / "data" / "ArmorLevels.json").read_text(encoding="utf-8"))
    mod_folder = OUTPUT / "ObjectModifications"
    mod_folder.mkdir()
    for row in levels["ballisticWeave"]:
        write(mod_folder, f"{row['editorID']} - {row['localFormID']}_Fallout4.esm.yaml", weave_yaml(row))

    print(f"{len(calibers) * 2} projectiles, {len(list(ammo_folder.iterdir()))} ammo records, {len(levels['ballisticWeave'])} weave mods in {OUTPUT}")


main()
