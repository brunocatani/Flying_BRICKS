"""Write src/TerminalData.inl from data/ArmorLevels.json, the registry and the game's masters.

python -I tools/generate_terminal_data.py <game Data folder>

The runtime has no editor IDs, so rules written against them are resolved here into plugin and form ID
rows. Every cartridge in the registry must have a stop level. Masters that are not installed are skipped.
"""
import json
import math
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
MASTERS = ("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm")
GAME_UNITS_PER_METRE = 70
METRES_PER_FOOT = 0.3048
# A round's deceleration is pi / 8 x air density x G1 drag x speed squared / ballistic coefficient, in SI units.
DRAG_CONSTANT = math.pi / 8.0 * 1.225
BC_TO_KG_PER_SQUARE_METRE = 703.07
NO_STOP_LEVEL = 5
ANY_RANGE = 0xFFFF
AMMO_LIST_OFFSET = 0x300  # A caliber's NPC ammo list is its normal projectile's local ID plus this; see generate_esp.py.
WORLD_AMMO_LIST = 0xBA0  # The list of every caliber loaded guns fire; see generate_esp.py.


def subrecords(body):
    position = 0
    while position + 6 <= len(body):
        kind = body[position : position + 4]
        size = struct.unpack_from("<H", body, position + 4)[0]
        yield kind, body[position + 6 : position + 6 + size]
        position += 6 + size


def editor_ids(path):
    """Yield (record type, local form ID, editor ID) for the plugin's own ARMO and OMOD records."""
    buffer = path.read_bytes()
    position = 0
    while position + 24 <= len(buffer):
        kind = buffer[position : position + 4]
        size = struct.unpack_from("<I", buffer, position + 4)[0]
        if kind == b"GRUP":
            position += 24
            continue
        flags, form_id = struct.unpack_from("<II", buffer, position + 8)
        if kind in (b"ARMO", b"OMOD"):
            body = buffer[position + 24 : position + 24 + size]
            if flags & 0x00040000:
                body = zlib.decompress(body[4:])
            edid = next((value.split(b"\0")[0].decode("latin1") for name, value in subrecords(body) if name == b"EDID"), "")
            yield kind, form_id & 0xFFFFFF, edid
        position += 24 + size


def shipped(registry):
    """The calibers the mod ships, by ID. Every registry caliber still needs complete data; one with "shipped": false
    is research data only and gets no runtime row (see generate_esp.py)."""
    return {row["id"]: row for row in registry["projectiles"] if row.get("shipped", True)}


def armor_rule(edid, rules):
    """First match wins, so specific prefixes sit above the wide lower-case "contains" rules."""
    for rule in rules:
        if "contains" in rule and rule["contains"] in edid.lower():
            return rule
        if "prefix" in rule and edid.startswith(rule["prefix"]):
            return rule
    return None


def main():
    data_folder = pathlib.Path(sys.argv[1])
    levels = json.loads((ROOT / "data" / "ArmorLevels.json").read_text(encoding="utf-8"))
    registry = json.loads((ROOT / "data" / "BallisticRegistry.json").read_text(encoding="utf-8"))

    stop_level = {}
    for level, ids in levels["stopLevels"].items():
        for cartridge in ids.split():
            if cartridge in stop_level:
                raise SystemExit(f"{cartridge} has two stop levels")
            stop_level[cartridge] = NO_STOP_LEVEL if level == "none" else int(level)
    profiles = {row["id"]: row for row in registry["projectiles"]}
    if set(stop_level) != set(profiles):
        raise SystemExit(f"stop levels and registry differ: {sorted(set(stop_level) ^ set(profiles))}")

    flight = {row["id"]: row for row in json.loads((ROOT / "data" / "CartridgeFlight.json").read_text(encoding="utf-8"))["cartridges"]}
    if set(flight) != set(profiles):
        raise SystemExit(f"flight data and registry differ: {sorted(set(flight) ^ set(profiles))}")

    burst = levels["headBurst"]
    shotguns = set(burst["shotguns"].split())
    caliber_rows = []
    for cartridge, row in sorted(shipped(registry).items(), key=lambda item: int(item[1]["normalLocalFormID"], 16)):
        energy = row["energyFtLb"]
        burst_range = 0
        if energy >= burst["atAnyRangeEnergyFtLb"]:
            burst_range = ANY_RANGE
        elif cartridge in shotguns:
            burst_range = burst["shotgunRangeMeters"] * GAME_UNITS_PER_METRE
        elif energy >= burst["atContactEnergyFtLb"]:
            burst_range = burst["contactRangeMeters"] * GAME_UNITS_PER_METRE
        severs = "true" if energy >= levels["severEnergyFtLb"] else "false"
        speed = flight[cartridge]["fps"] * METRES_PER_FOOT * GAME_UNITS_PER_METRE
        drag = DRAG_CONSTANT / (flight[cartridge]["bc"] * BC_TO_KG_PER_SQUARE_METRE)
        caliber_rows.append(
            f'    CaliberRow{{ 0x{int(row["normalLocalFormID"], 16):06X}, {energy:.1f}F, {stop_level[cartridge]}, {burst_range}, {severs}, {speed:.1f}F, {drag:.8f}F }},  // {cartridge}'
        )

    armor_rows = []
    heavy_rows = []
    heavy_pattern = re.compile(levels["heavyModPattern"])
    weave = {int(row["localFormID"], 16): row for row in levels["ballisticWeave"]}
    weave_found = set()
    for master in MASTERS:
        path = data_folder / master
        if not path.exists():
            print(f"skipped {master}: not installed")
            continue
        for kind, local, edid in editor_ids(path):
            # Records the master overrides belong to an earlier file; only its own IDs are rows here.
            if kind == b"ARMO":
                rule = armor_rule(edid, levels["armorRules"])
                if rule:
                    armor_rows.append((master.lower(), local, rule["level"], rule["heavyLevel"], edid))
            elif heavy_pattern.match(edid):
                heavy_rows.append((master.lower(), local, edid))
            elif master == "Fallout4.esm" and local in weave and edid == weave[local]["editorID"]:
                weave_found.add(local)

    if weave_found != set(weave):
        raise SystemExit(f"weave mods not found in Fallout4.esm: {sorted(set(weave) - weave_found)}")

    armor_rows.sort()
    heavy_rows.sort()

    creatures = json.loads((ROOT / "data" / "CreatureLevels.json").read_text(encoding="utf-8"))["races"]
    max_zones = max(len(row["zones"]) for row in creatures)
    creature_rows = []
    for row in creatures:
        zones = ", ".join(
            f'CreatureZone{{ {zone["limb"]}, {zone.get("level", row["level"])}, {zone.get("factor", 1.0)}F, {"true" if zone.get("head") else "false"} }}'
            for zone in row["zones"]
        )
        flags = ", ".join("true" if row.get(key) else "false" for key in ("headKill", "vanillaRemoval"))
        creature_rows.append(
            f'    CreatureRow{{ "{row["plugin"].lower()}", 0x{int(row["id"], 16):06X}, {row["level"]}, {flags}, {len(row["zones"])}, {{ {zones} }} }},  // {row["name"]}'
        )
    rated = levels["ratedEnergyFtLb"]
    lines = [
        "// Generated by tools/generate_terminal_data.py from data/ArmorLevels.json. Do not edit.",
        "",
        f'inline constexpr std::array<float, 5> kRatedEnergy{{ 0.0F, {rated["1"]}.0F, {rated["2"]}.0F, {rated["3"]}.0F, {rated["4"]}.0F }};',
        f"inline constexpr std::uint8_t kNoStopLevel = {NO_STOP_LEVEL};",
        f"inline constexpr std::uint16_t kAnyRange = {ANY_RANGE};",
        "",
        "// Sorted by the caliber's normal projectile, which identifies it on every ammunition record.",
        f"inline constexpr std::array<CaliberRow, {len(caliber_rows)}> kCaliberRows{{",
        *caliber_rows,
        "};",
        "",
        f"inline constexpr std::array<ArmorRow, {len(armor_rows)}> kArmorRows{{",
        *(f'    ArmorRow{{ "{master}", 0x{local:06X}, {level}, {heavy} }},  // {edid}' for master, local, level, heavy, edid in armor_rows),
        "};",
        "",
        f"inline constexpr std::array<HeavyModRow, {len(heavy_rows)}> kHeavyModRows{{",
        *(f'    HeavyModRow{{ "{master}", 0x{local:06X} }},  // {edid}' for master, local, edid in heavy_rows),
        "};",
        "",
        f"inline constexpr std::array<WeaveModRow, {len(weave)}> kWeaveModRows{{",
        *(f'    WeaveModRow{{ 0x{local:06X}, {row["level"]} }},  // {row["editorID"]}' for local, row in sorted(weave.items())),
        "};",
        "",
        f"inline constexpr std::size_t kMaxCreatureZones = {max_zones};",
        "",
        "struct CreatureRow",
        "{",
        "    std::string_view plugin;",
        "    std::uint32_t local;",
        "    std::uint8_t level;",
        "    bool headKill;",
        "    bool vanillaRemoval;",
        "    std::uint8_t zoneCount;",
        "    std::array<CreatureZone, kMaxCreatureZones> zones;",
        "};",
        "",
        f"inline constexpr std::array<CreatureRow, {len(creature_rows)}> kCreatureRows{{",
        *creature_rows,
        "};",
    ]
    output = ROOT / "src" / "TerminalData.inl"
    output.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print(f"{len(caliber_rows)} calibers, {len(armor_rows)} armor rows, {len(heavy_rows)} heavy mods")
    for row in armor_rows:
        if row[2] > 0:
            print(row)
    write_compatibility()


def literal(text):
    """A C++ string literal; JSON's escaping is valid C++ for these ASCII paths."""
    return json.dumps(text or "")


def write_compatibility():
    registry = json.loads((ROOT / "data" / "BallisticRegistry.json").read_text(encoding="utf-8"))
    compat = json.loads((ROOT / "data" / "Compatibility.json").read_text(encoding="utf-8"))
    plugin_name = registry["plugin"]
    profiles = shipped(registry)

    # The item of ours that stands for each caliber: its added item, or the playable vanilla record.
    ours = {}
    for profile in profiles.values():
        if "ammoLocalFormID" in profile:
            ours[profile["id"]] = (plugin_name, int(profile["ammoLocalFormID"], 16))
    for row in registry["vanillaAmmo"]:
        if not row["notPlayable"] and row["profile"] not in ours:
            ours[row["profile"]] = (row["plugin"], int(row["localFormID"], 16))
    missing = sorted(set(profiles) - set(ours))
    if missing:
        raise SystemExit(f"calibers with no item of ours: {missing}")

    def ammo_row(plugin, local, caliber):
        profile = profiles[caliber]
        return (
            f'    AmmoRow{{ {literal(plugin)}, 0x{local:06X}, 0x{int(profile["normalLocalFormID"], 16):06X}, '
            f'{float(profile["damage"])}F }},  // {caliber}'
        )

    rows = []
    for profile in profiles.values():
        if "ammoLocalFormID" in profile:
            rows.append(ammo_row(plugin_name, int(profile["ammoLocalFormID"], 16), profile["id"]))
    for row in registry["vanillaAmmo"]:
        rows.append(ammo_row(row["plugin"], int(row["localFormID"], 16), row["profile"]))
    (ROOT / "src" / "AmmoData.inl").write_text("\n".join([
        "// Generated by tools/generate_terminal_data.py from the registry. Do not edit.",
        "",
        f"inline constexpr std::array<AmmoRow, {len(rows)}> kAmmoRows{{",
        *rows,
        "};",
        "",
    ]), encoding="ascii", newline="\n")

    providers = compat["providers"]
    visual_rows = []
    for profile in sorted(profiles.values(), key=lambda row: int(row["normalLocalFormID"], 16)):
        visual = compat["visuals"].get(profile["id"], {})
        armament = visual.get("armament", {})
        munitions = visual.get("munitions", {})
        wof = compat["wofFlight"][profile["trajectory"]]
        bibfx = compat["bibfx"]["flightByClass"][profile["presentationClass"]]
        visual_rows.append(
            f'    VisualRow{{ 0x{int(profile["normalLocalFormID"], 16):06X}, '
            f'{literal(armament.get("flight"))}, {literal(armament.get("box"))}, {literal(armament.get("casing"))}, '
            f'{"true" if armament.get("exact") else "false"}, '
            f'{literal(munitions.get("flight"))}, {literal(munitions.get("box"))}, {literal(munitions.get("casing"))}, '
            f'{literal(wof["normal"])}, {literal(wof["suppressed"])}, {literal(bibfx["normal"])}, {literal(bibfx["suppressed"])}, {"true" if bibfx["impact"] else "false"} }},  // {profile["id"]}'
        )
    (ROOT / "src" / "CompatibilityData.inl").write_text("\n".join([
        "// Generated by tools/generate_terminal_data.py from data/Compatibility.json. Do not edit.",
        "",
        "// Lowest priority first.",
        "inline constexpr std::array<ProviderRow, 4> kProviders{",
        *(f'    ProviderRow{{ {literal(providers[name]["plugin"])}, {literal(providers[name]["sampleMesh"])} }},  // {name}' for name in ("munitions", "armament", "bibfx", "wof")),
        "};",
        f'inline constexpr std::string_view kBibfxPlugin = {literal(providers["bibfx"]["plugin"])};',
        f'inline constexpr std::string_view kCaliberComplexPlugin = {literal(compat["caliberComplexPlugin"])};',
        f'inline constexpr std::uint32_t kBibfxNormalExplosion = 0x{compat["bibfx"]["normalExplosion"]};',
        f'inline constexpr std::uint32_t kBibfxSuppressedExplosion = 0x{compat["bibfx"]["suppressedExplosion"]};',
        f'inline constexpr std::string_view kOurPlugin = {literal(plugin_name)};',
        f"inline constexpr std::uint32_t kWorldAmmoList = 0x{WORLD_AMMO_LIST:06X};",
        "inline constexpr std::uint32_t kWorldAmmoHost = 0x06738B;  // LL_Ammo_Any, read to tell whether RobCo Patcher added the list.",
        "",
        "// Sorted by the caliber's normal projectile.",
        f"inline constexpr std::array<VisualRow, {len(visual_rows)}> kVisualRows{{",
        *visual_rows,
        "};",
        "",
    ]), encoding="ascii", newline="\n")
    print(f"{len(rows)} ammo rows, {len(visual_rows)} visual rows")
    write_calibers(ours, profiles)


def write_calibers(ours, profiles):
    def row_text(caliber, profile):
        normal = int(profile["normalLocalFormID"], 16)
        plugin, local = ours[caliber]
        world = "false" if plugin.lower() == "fallout4.esm" else "true"
        return (
            f'    CaliberRow{{ {literal(caliber)}, {literal(plugin)}, 0x{local:06X}, 0x{normal:06X}, '
            f'{float(profile["damage"])}F, 0x{normal + AMMO_LIST_OFFSET:06X}, {world} }},'
        )

    rows = [row_text(caliber, profile) for caliber, profile in profiles.items()]
    (ROOT / "src" / "CaliberData.inl").write_text("\n".join([
        "// Generated by tools/generate_terminal_data.py from the registry. Do not edit.",
        "",
        f"inline constexpr std::array<CaliberRow, {len(rows)}> kCaliberRows{{",
        *rows,
        "};",
        "",
    ]), encoding="ascii", newline="\n")
    print(f"{len(rows)} calibers")


main()
