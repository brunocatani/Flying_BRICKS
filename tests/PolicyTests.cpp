#include "DamagePolicy.h"

// Guards the scope contract: the authored model owns only our ammunition fired from a ranged
// weapon. Every other attack must reach the native formula, or it loses or gains damage it should not.
namespace policy = flying_bricks::damage_policy;

namespace
{
    constexpr std::uint8_t kOneHandSword = 1;
    constexpr std::uint8_t kGun = 9;
    constexpr std::uint32_t kNoFlags = 0;

    static_assert(policy::usesAuthoredDamage(kGun, true, false));
    static_assert(!policy::usesAuthoredDamage(kGun, false, false), "foreign ammunition stays native");
    static_assert(!policy::usesAuthoredDamage(kGun, true, true), "a gun bash stays native");
    static_assert(!policy::usesAuthoredDamage(kOneHandSword, true, false), "melee weapons stay native");

    static_assert(policy::isAuthoredHit(kNoFlags, kGun, true));
    static_assert(!policy::isAuthoredHit(kNoFlags, kGun, false), "foreign ammunition keeps perks and criticals");
    static_assert(!policy::isAuthoredHit(policy::kExplosionFlag, kGun, true), "explosions stay native");
    static_assert(!policy::isAuthoredHit(policy::kBashFlag, kGun, true), "a gun bash keeps its multipliers");
    static_assert(!policy::isAuthoredHit(policy::kTimedBashFlag, kGun, true));
    static_assert(!policy::isAuthoredHit(policy::kPowerAttackFlag, kGun, true));
    static_assert(!policy::isAuthoredHit(policy::kMeleeAttackFlag, kGun, true));
}

int main()
{
    return 0;
}
