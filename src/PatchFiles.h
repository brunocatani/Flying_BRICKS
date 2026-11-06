#pragma once

#include "PCH.h"

#include "PatchFormat.h"

#include <span>

// Reads the patch files in Data\F4SE\Plugins\Flying_BRICKS\Patches. A file is named after the plugin it patches, such as
// Glock19x.esp.toml, and it applies to that plugin's .esp, .esl or .esm version, whichever is loaded. A file for a plugin
// with no version loaded is not opened. Files live in the mod folder rather than the
// Documents settings folder because they are game data other mods ship: MO2 layers them, and a higher mod's file of the same
// name replaces a lower one. See PatchFormat.h for the format.
//
// Thread model: load() runs once on the F4SE message thread at GameLoaded. It reads the files before the game plays, never
// per frame. The forms it returns live for the whole session, and nothing here keeps state afterwards.
namespace flying_bricks::patch_files
{
    struct RoundPatch
    {
        RE::TESAmmo* ammo{};
        RE::TESAmmo* replacement{};
        std::uint32_t normalProjectile{};
        float damage{};
    };

    struct WeaponPatch
    {
        RE::TESObjectWEAP* weapon{};
        RE::TESAmmo* ammo{};
        RE::TESLevItem* ammoList{};  // Null keeps the weapon's own list.
    };

    struct PartPatch
    {
        RE::BGSMod::Attachment::Mod* part{};
        RE::TESAmmo* ammo{};
    };

    struct DamagePatch
    {
        RE::TESObjectWEAP* weapon{};
        std::uint16_t damage{};
    };

    struct DefaultPatch
    {
        RE::TESObjectWEAP* weapon{};
        RE::BGSMod::Attachment::Mod* from{};
        RE::BGSMod::Attachment::Mod* to{};
    };

    struct Patches
    {
        std::vector<RoundPatch> rounds;
        std::vector<WeaponPatch> weapons;
        std::vector<PartPatch> parts;
        std::vector<DamagePatch> damage;
        std::vector<DefaultPatch> defaults;
        std::size_t filesRead{};
        std::size_t filesNotLoaded{};
        std::size_t problems{};
        std::size_t formsNotFound{};
    };

    [[nodiscard]] Patches load();

    // Every caliber of ours, from the ballistic registry.
    [[nodiscard]] std::span<const patch_format::CaliberRow> calibers() noexcept;
}
