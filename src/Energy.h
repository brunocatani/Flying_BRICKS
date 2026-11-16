#pragma once

#include "PatchFiles.h"
#include "Terminal.h"

#include <cstddef>
#include <optional>
#include <span>

namespace RE
{
    class ActorValueInfo;
    class BGSProjectile;
    class TESAmmo;
    class TESObjectWEAP;
}

// Laser, plasma and fire attacks on the caliber scale (data/EnergyModel.json, docs/DamageModel.md).
//
// A weapon's family comes from its projectile override when that is a flame stream, else from its ammunition: the
// base game's in data/EnergyModel.json, other mods' through [[energy]] rows of their patch files.
// The engine applies each typed damage of a hit as the weapon's value times a power (range falloff, charge,
// projectile count); the model turns that native value into damage with the caliber model's energy exponent,
// so every attachment, a musket's charge and a splitter's beams scale damage the way energy scales a round.
//
// Thread model: build() runs once on the F4SE message thread before any hook is patched and the tables never
// change afterwards. The queries run on any hit thread and only read engine data.
namespace flying_bricks::energy
{
    struct BuildResult
    {
        std::size_t ammunition{};
        std::size_t projectiles{};
        std::size_t flameProjectiles{};
        std::size_t patchedAmmunition{};
        std::size_t patchedProjectiles{};
        bool incendiaryThrow{};
    };

    // patched: other mods' ammunition and projectiles from patch files. A row for a form the model already has
    // replaces its family, so a patch file's row wins.
    [[nodiscard]] BuildResult build(std::span<const patch_files::EnergyPatch> patched);

    // The family of a ranged weapon's shots: its projectile override's family when the model knows the projectile
    // (a converted plasma gun firing a laser's cell), else its ammunition's, else nothing for a weapon the model
    // does not own.
    [[nodiscard]] std::optional<terminal::Thermal> familyOf(const RE::BGSProjectile* overrideProjectile, const RE::TESAmmo* ammo) noexcept;

    // True when the weapon instance, with its attachments, deals energy typed damage: the model's input. A gun on an
    // energy round that deals only physical damage (a converted Gauss rifle on fusion cells) is not an energy shot.
    [[nodiscard]] bool carriesEnergyDamage(const RE::TESObjectWEAP::InstanceData& instance) noexcept;

    // A thrown weapon whose explosion burns (a Molotov).
    [[nodiscard]] bool isIncendiaryThrow(const RE::TESObjectWEAP& weapon) noexcept;

    // The model's damage for a native typed value, before armor.
    [[nodiscard]] float authoredDamage(terminal::Thermal family, float nativeValue) noexcept;

    // The model's damage for an incendiary throw's native explosion damage, before armor.
    [[nodiscard]] float incendiaryThrowDamage(float nativeDamage) noexcept;

    // False for a beam, which keeps its energy over game ranges.
    [[nodiscard]] bool keepsRangeFalloff(terminal::Thermal family) noexcept;

    // The resistance the energy damage type reads, which tells the energy curve call from any other typed damage.
    [[nodiscard]] const RE::ActorValueInfo* energyResistance() noexcept;
}
