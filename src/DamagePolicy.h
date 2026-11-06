#pragma once

#include <cstdint>

// Decides which attacks the authored damage model owns. Everything it rejects stays on the
// untouched native formula, so explosions, energy weapons and foreign ammunition keep their damage.
namespace flying_bricks::damage_policy
{
    // WEAPON_TYPE values; 0..6 are hand-to-hand and melee.
    inline constexpr std::uint8_t kBow = 7;
    inline constexpr std::uint8_t kMine = 11;

    // HitData::flags bits, set by the native hit initializers in Fallout4VR.exe 1.2.72.0.
    inline constexpr std::uint32_t kPredictDamageFlag = 1u << 13;
    inline constexpr std::uint32_t kPredictBaseDamageFlag = 1u << 14;
    inline constexpr std::uint32_t kBashFlag = 1u << 15;
    inline constexpr std::uint32_t kTimedBashFlag = 1u << 16;
    inline constexpr std::uint32_t kPowerAttackFlag = 1u << 17;
    inline constexpr std::uint32_t kMeleeAttackFlag = 1u << 18;
    inline constexpr std::uint32_t kExplosionFlag = 1u << 20;

    [[nodiscard]] constexpr bool isRangedWeaponType(std::uint8_t weaponType) noexcept
    {
        return weaponType >= kBow && weaponType <= kMine;
    }

    [[nodiscard]] constexpr bool isMeleeAction(std::uint32_t hitFlags) noexcept
    {
        constexpr auto meleeMask = kBashFlag | kTimedBashFlag | kPowerAttackFlag | kMeleeAttackFlag;
        return (hitFlags & meleeMask) != 0;
    }

    [[nodiscard]] constexpr bool isPrediction(std::uint32_t hitFlags) noexcept
    {
        return (hitFlags & (kPredictDamageFlag | kPredictBaseDamageFlag)) != 0;
    }

    // A weapon formula evaluation: the model value replaces it only for our ammunition fired from a ranged weapon.
    [[nodiscard]] constexpr bool usesAuthoredDamage(std::uint8_t weaponType, bool ownedAmmo, bool meleeAction) noexcept
    {
        return ownedAmmo && !meleeAction && isRangedWeaponType(weaponType);
    }

    // A blow by a weapon or a fist. The melee model owns it, not the ammunition: a gun's bash carries a gun's ammo.
    [[nodiscard]] constexpr bool isMeleeBlow(std::uint32_t hitFlags) noexcept
    {
        return (hitFlags & kExplosionFlag) == 0 && isMeleeAction(hitFlags);
    }

    // A hit in progress: its perk, critical, sneak, blocking and difficulty scaling is bypassed only when
    // the model owns it. An explosion carrying our ammunition's weapon is still an explosion.
    [[nodiscard]] constexpr bool isAuthoredHit(std::uint32_t hitFlags, std::uint8_t weaponType, bool ownedAmmo) noexcept
    {
        return (hitFlags & kExplosionFlag) == 0 && usesAuthoredDamage(weaponType, ownedAmmo, isMeleeAction(hitFlags));
    }
}
