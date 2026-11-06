#pragma once

#include <cstddef>
#include <span>

namespace RE
{
    class BGSProjectile;
    class TESAmmo;
}

// The ammunition the authored model owns: the records Flying_BRICKS.esp provides, vanilla overrides and
// additions alike, plus other mods' records for the same calibers, whichever plugin or patcher wrote them last.
// Ownership comes from the generated table and the patch files, never from a record's live values, and build() writes
// our damage and projectile back onto every owned record.
//
// Thread model: build() runs once on the F4SE message thread before any hook is patched and the
// table never changes afterwards, so the hooks read it from any thread without locking.
namespace flying_bricks::terminal
{
    struct Caliber;
}

namespace flying_bricks::patch_files
{
    struct RoundPatch;
}

namespace flying_bricks::ammo_table
{
    struct Entry
    {
        // Non-owning engine forms, which live for the whole session.
        RE::TESAmmo* ammo{};
        RE::BGSProjectile* normal{};
        RE::BGSProjectile* suppressed{};
        const terminal::Caliber* caliber{};  // Static table; null keeps the native armor curve.
        float damage{};
        bool foreign{};                 // Another mod's record for one of our calibers.
        RE::TESAmmo* replacement{};     // The item of ours a foreign record is replaced with.
    };

    struct BuildResult
    {
        std::size_t owned{};
        std::size_t foreign{};
        // Records we override whose winning version belongs to a later plugin. Still ours, but their name and
        // other fields are that plugin's, so the plugin should load last.
        std::size_t overriddenByLaterPlugin{};
    };

    // rounds are other mods' records for our calibers, from the patch files.
    [[nodiscard]] BuildResult build(std::span<const patch_files::RoundPatch> rounds);

    [[nodiscard]] const Entry* find(const RE::TESAmmo* ammo) noexcept;

    [[nodiscard]] std::span<const Entry> entries() noexcept;

    // True when a weapon's override projectile is an ordinary bullet that the caliber's projectile
    // may stand in for. Explosive, incendiary, flare and similar projectiles must be preserved.
    [[nodiscard]] bool isReplaceable(const RE::BGSProjectile& projectile) noexcept;

    [[nodiscard]] bool isSuppressed(const RE::BGSProjectile& projectile) noexcept;
}
