#pragma once

#include <cstddef>

// Lends visuals from other mods while Flying_BRICKS has no models of its own, and replaces their ammunition
// with ours. Providers are optional and never masters of the plugin: without them our records keep the vanilla
// models the ESP authors. Numbers, flight and terminal behaviour never come from a provider.
//
// Also applies the patch files, which name the round for weapons from other mods. They are ours rather than RobCo Patcher
// files because RobCo's own files override each other by file order, and this runs after all of them.
//
// Thread model: apply() runs once on the F4SE message thread at GameLoaded, after RobCo Patcher's pass at
// GameDataReady and before any hook is patched. It only writes engine forms, and nothing reads them yet.
namespace flying_bricks::patch_files
{
    struct Patches;
}

namespace flying_bricks::compatibility
{
    struct ApplyResult
    {
        std::size_t providers{};
        std::size_t models{};
        std::size_t weapons{};
        std::size_t objectMods{};
        std::size_t leveledLists{};
        std::size_t formLists{};
        std::size_t recipes{};
        std::size_t containers{};
        std::size_t patchedWeapons{};
        std::size_t patchedObjectMods{};
        std::size_t restoredDamage{};
        std::size_t swappedDefaults{};
        std::size_t worldCalibers{};
    };

    [[nodiscard]] ApplyResult apply(const patch_files::Patches& patches);

    // Shows a notification when apply() found a mod we do not support. Call on the message thread after a game loads.
    void warnIfIncompatible();
}
