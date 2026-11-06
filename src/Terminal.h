#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace RE
{
    class Actor;
    class BGSProjectile;
}

// What a round does to a target: worn armor level against the caliber's real stop level, limb factors,
// the instant head kill, head explosion and limb removal. Humans, ghouls, gen 3 synths (which use the human
// race) and super mutants out of power armor read worn armor. Creatures read data/CreatureLevels.json, their
// body taken as armor on the same scale. Any race on neither list stays on the native curve.
//
// Power armor has its own rule per suit piece. Only anti-materiel rounds go through a piece; anything less
// reaches the wearer as blunt force that grows with energy, and handgun energy not at all. Each hit also
// wears the piece. The game keeps the piece's health and takes it off at zero, which exposes that limb, and
// an exposed limb under the frame takes the round like bare skin.
//
// Thread model: build() runs once on the F4SE message thread before any hook is patched and the tables
// never change afterwards. resolve() and the queries run on any hit thread and only read engine data.
namespace flying_bricks::terminal
{
    // Above the worn scale, so the hit trace tells a suit piece from any worn armor.
    inline constexpr std::uint8_t kPlateLevel = 6;

    // Share of a melee blow that gets through, by worn armor level, then behind an intact suit piece.
    struct Protection
    {
        std::array<float, 5> worn{};
        float plate{};
    };

    // energyFtLb is the muzzle energy, which places the round in its class (stop level, whether it crosses a
    // power armor piece at all, whether a suit passes any of it). energyKept is the share of it the round still
    // carries at impact (presets::atDistance), which sets the amounts: damage, what survives the armor, what a
    // suit passes and how much it wears the piece.
    struct Caliber
    {
        float energyFtLb{};
        std::uint8_t stopLevel{};  // First armor level that stops the round; 5 is none.
        std::uint16_t headBurstRange{};  // Game units; 0 is never and 0xFFFF is any range.
        bool severs{};
        float realWorldSpeed{};  // Muzzle velocity in game units per second.
        float dragFactor{};      // Per metre: deceleration is this x G1 drag x speed squared, in SI units.
        float energyKept{ 1.0F };  // 1 at the muzzle; the table's calibers never change it.
    };

    struct Outcome
    {
        bool active{};  // False leaves the hit entirely native.
        std::uint8_t armorLevel{};  // kPlateLevel on an intact power armor piece.
        bool stopped{};
        bool instantKill{};
        float armorFraction{ 1.0F };  // Replaces the native armor curve's result.
        float limbFactor{ 1.0F };  // Replaces the native body part multiplier.
        bool head{};
        bool vanillaRemoval{};  // The game's own burst and sever rolls decide.
    };

    struct BuildResult
    {
        std::size_t armorRows{};
        std::size_t heavyMods{};
        std::size_t weaveMods{};
        std::size_t creatures{};
        std::size_t armorByRating{};  // Armor with a rating and no template; each is logged.
    };

    [[nodiscard]] BuildResult build();

    // The caliber of ammunition whose normal projectile is this one, or null.
    [[nodiscard]] const Caliber* findCaliber(const RE::BGSProjectile& normalProjectile) noexcept;

    // The instant kill on the player, for the INI switch. Atomic: read on every hit thread.
    void setPlayerHeadshotInstantKill(bool enabled) noexcept;

    // pellets is the weapon's projectile count: one shotgun pellet carries only its share of the energy.
    [[nodiscard]] Outcome resolve(RE::Actor& target, const Caliber& caliber, std::uint32_t limb, std::uint32_t pellets) noexcept;

    // What one round takes from the power armor piece it hits, in the form the native write expects.
    [[nodiscard]] float pieceDamage(const Caliber& caliber, std::uint32_t pellets) noexcept;

    // The same from a known wear in points.
    [[nodiscard]] float pieceDamage(float wear) noexcept;

    // A melee blow against the same armor, on the same limbs. Nothing here kills outright: the blow's own number
    // does that, and only a bare head doubles it.
    [[nodiscard]] Outcome resolveMelee(RE::Actor& target, const Protection& protection, std::uint32_t limb) noexcept;

    // Limb removal and head explosion for one body part of a killing hit. Only the hit limb qualifies.
    struct Removal
    {
        bool sever{};
        bool burst{};
    };
    [[nodiscard]] Removal removal(const Caliber& caliber, const Outcome& outcome, std::uint32_t hitLimb, std::uint32_t partLimb, std::uint8_t partFlags, float distance) noexcept;

    // The share of a round's authored damage it still carries at impact. Uses the damage model's own energy
    // exponent, so damage follows the energy at impact the way it follows it at the muzzle.
    [[nodiscard]] float damageShare(const Caliber& impact) noexcept;

    // The cone half angle of a real shotgun pattern, in radians.
    [[nodiscard]] float shotgunConeRadians() noexcept;
}
