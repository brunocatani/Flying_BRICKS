#include "PCH.h"

#include "Hooks.h"

#include "AmmoTable.h"
#include "DamagePolicy.h"
#include "HitLabels.h"
#include "Melee.h"
#include "Presets.h"
#include "Terminal.h"

#include <cmath>
#include <intrin.h>
#include <limits>
#include <utility>

namespace flying_bricks::hooks
{
    namespace
    {
        using InstanceData = RE::TESObjectWEAP::InstanceData;
        using RangedData = RE::TESObjectWEAP::RangedData;
        using WeaponInstance = RE::BGSObjectInstanceT<RE::TESObjectWEAP>;

        // Fields read on the hit and fire paths, checked against raw disassembly of
        // Fallout4VR.exe 1.2.72.0 (InitializeWeaponHit, the projectile hit pipeline and Fire).
        static_assert(offsetof(RE::HitData, weapon) == 0x58);
        static_assert(offsetof(RE::HitData, ammo) == 0x80);
        static_assert(offsetof(RE::HitData, healthDamage) == 0x90);
        static_assert(offsetof(RE::HitData, flags) == 0xC4);
        static_assert(offsetof(InstanceData, ammo) == 0x68);
        static_assert(offsetof(InstanceData, rangedData) == 0x98);
        static_assert(offsetof(InstanceData, soundLevel) == 0x114);
        static_assert(offsetof(InstanceData, type) == 0x137);
        static_assert(offsetof(RangedData, overrideProjectile) == 0x00);
        static_assert(offsetof(RE::TESAmmo, data) == 0x160);
        static_assert(offsetof(RE::AMMO_DATA, damage) == 0x10);
        static_assert(offsetof(RE::Projectile, velocity) == 0x18C);  // The velocity getter and the gravity step.

        using CalcWeaponDamageFunction = float (*)(RE::TESForm*, InstanceData*, RE::TESAmmo*, float, float);
        using DisplayDamageFunction = float (*)(WeaponInstance*, RE::TESAmmo*, float);
        using InitializeWeaponHitFunction = void (*)(RE::HitData*, RE::Actor*, RE::Actor*, WeaponInstance*, std::uint32_t, bool);
        using HitDataVoidFunction = void (*)(RE::HitData*);
        using HitDataFloatFunction = float (*)(RE::HitData*);
        using CollectDamageFunction = void (*)(RE::HitData*, WeaponInstance*, float);
        using ReduceDamageFunction = void (*)(RE::HitData*, bool);
        using RangeFalloffFunction = float (*)(RE::TESForm*, WeaponInstance*, float);
        using PlayerHitStateFunction = float (*)(void*);
        using BlockFractionFunction = float (*)(float, float, float, bool, bool, bool);
        using ActorHitFunction = void (*)(RE::Actor*, RE::HitData*);
        using DifficultyMultiplierFunction = float (*)(std::int32_t, void*, bool);
        using DifficultyScaleFunction = float (*)(RE::Actor*, float);
        using EntryPointSixFunction = void (*)(std::uint32_t, RE::Actor*, void*, void*, void*, void*);
        using EntryPointFiveFunction = void (*)(std::uint32_t, RE::Actor*, void*, void*, void*);
        using EntryPointFloatFunction = void (*)(std::uint32_t, RE::Actor*, float*);
        using FireFunction = void (*)(WeaponInstance*, RE::TESObjectREFR*, std::uint32_t, RE::TESAmmo*, RE::AlchemyItem*);
        using ArmorCurveFunction = float (*)(void*, float, float);
        using LimbMultiplierFunction = void (*)(RE::HitData*);
        using PelletSpreadFunction = void (*)(void*, float, float, float, void*);
        using ConeRangeFunction = void (*)(void*, float*, float*);
        using RemovalDecisionFunction = void (*)(RE::Actor*, void*, RE::HitData*, WeaponInstance*, bool, bool*, bool*);
        using MissileMoveFunction = void (*)(RE::Projectile*, float);
        using PieceDamageFunction = float (*)(RE::Actor*, RE::HitData*, RE::TESForm*, RE::ExtraDataList*);

        constexpr std::uintptr_t kPieceDamageTarget = 0x009C34D0;
        constexpr std::uintptr_t kCalcWeaponDamageTarget = 0x0064D3C0;
        constexpr std::uintptr_t kDisplayDamageTarget = 0x0064FFD0;
        constexpr std::uintptr_t kInitializeWeaponHitTarget = 0x01042950;
        constexpr std::uintptr_t kAdjustAttackDamageTarget = 0x01045A90;
        constexpr std::uintptr_t kCriticalDamageTarget = 0x01046300;
        constexpr std::uintptr_t kCollectDamageTarget = 0x010445E0;
        constexpr std::uintptr_t kEntryPointTarget = 0x0054B7E0;
        constexpr std::uintptr_t kReduceDamageTarget = 0x01044720;
        constexpr std::uintptr_t kRangeFalloffTarget = 0x0064FE50;
        constexpr std::uintptr_t kPlayerHitStateTarget = 0x010652D0;
        constexpr std::uintptr_t kBlockFractionTarget = 0x0064E300;
        constexpr std::uintptr_t kActorHitTarget = 0x00E526D0;
        constexpr std::uintptr_t kDifficultyMultiplierTarget = 0x00652FE0;
        constexpr std::uintptr_t kDifficultyScaleTarget = 0x00DDD8A0;
        constexpr std::uintptr_t kFireTarget = 0x00333740;
        constexpr std::uintptr_t kArmorCurveTarget = 0x0064DF70;
        constexpr std::uintptr_t kLimbMultiplierTarget = 0x01047600;
        constexpr std::uintptr_t kPelletSpreadTarget = 0x00835040;
        constexpr std::uintptr_t kConeRangeTarget = 0x00835860;
        constexpr std::uintptr_t kRemovalDecisionTarget = 0x00E1A6D0;
        constexpr std::uintptr_t kMissileMoveTarget = 0x01056BF0;

        // Called directly rather than patched, so preflight checks the spread function's own call to the
        // cone range and the power armor test's first bytes.
        constexpr std::uintptr_t kConeRangeCallInSpread = 0x008350EB;
        constexpr std::uintptr_t kActorInPowerArmorTarget = 0x009BF5D0;
        constexpr std::array<std::uint8_t, 15> kActorInPowerArmorBytes{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20 };

        // AimModel fields and BGSBodyPart's limb, read from raw disassembly of the spread function, the
        // limb multiplier and the removal decision. The spread output array's count sits at +0x18.
        constexpr std::size_t kAimModelConeBlend = 0x90;
        constexpr std::size_t kBodyPartFlags = 0x114;
        constexpr std::size_t kBodyPartLimb = 0x115;
        constexpr std::size_t kSpreadOutputCount = 0x18;

        // HitData::damageLimb values, read from the hit event's limb test and the hit trace.
        constexpr std::uint32_t kNoLimb = 0xFFFFFFFF;
        constexpr std::uint32_t kTorsoLimb = 0;

        // Every direct CalcWeaponDamage caller: display, projectile launch, ammo-less wrapper, hit data.
        constexpr std::array<std::uintptr_t, 4> kCalcWeaponDamageCallsites{ 0x00650039, 0x01057C6A, 0x0064D3B0, 0x01042CB2 };

        // GetWeaponDisplayDamage applies player perks after the formula, so the item card needs its own override.
        constexpr std::array<std::uintptr_t, 4> kDisplayDamageCallsites{ 0x00B114AF, 0x00C1E22D, 0x00BC13F4, 0x00C50F75 };

        // InitializeWeaponHit's last argument marks a melee action. It nulls HitData::ammo for those,
        // so the formula hook needs this scope to tell a bash from a shot with a null ammo argument.
        constexpr std::array<std::uintptr_t, 7> kInitializeWeaponHitCallsites{
            0x01043C06, 0x01042778, 0x00E58336, 0x00E587F2, 0x00E4FD20, 0x00E5704C, 0x01051649
        };

        // Weapon, explosion and projectile pipelines. AdjustAttackDamage owns actor, sneak, VATS and effect multipliers.
        constexpr std::array<std::uintptr_t, 3> kAdjustAttackDamageCallsites{ 0x01042F3F, 0x010436DC, 0x010432B3 };
        constexpr std::array<std::uintptr_t, 3> kCriticalDamageCallsites{ 0x01042CF1, 0x01043607, 0x01043242 };

        // The weapon's range stats scale a hit by distance: one caller for weapon hits, one for projectiles.
        constexpr std::array<std::uintptr_t, 2> kRangeFalloffCallsites{ 0x01042C72, 0x01052BD6 };

        // CollectDamage still applies typed damage and resistance; only its perk entry points are skipped.
        constexpr std::array<std::uintptr_t, 3> kCollectDamageCallsites{ 0x01042D2C, 0x0104362F, 0x01043269 };
        constexpr std::array<std::uintptr_t, 3> kTypedEntryPointCallsites{ 0x01046B25, 0x01046B59, 0x01046B91 };

        // ReduceDamage stays native for its bookkeeping, and for targets the terminal model does not
        // cover it still applies the armor curve. Its perk entry points, the
        // stat-based block fraction and the multiplier it applies to hits on the player while
        // the state read at 0x010652D0 holds (not difficulty) are skipped.
        constexpr std::array<std::uintptr_t, 6> kReduceDamageCallsites{ 0x0104399C, 0x01042FC1, 0x01043702, 0x01043316, 0x01043467, 0x01042872 };
        constexpr std::array<std::uintptr_t, 2> kReduceFiveEntryPointCallsites{ 0x0104487A, 0x01044E1F };
        constexpr std::array<std::uintptr_t, 1> kReduceFloatEntryPointCallsites{ 0x01044C90 };
        constexpr std::array<std::uintptr_t, 1> kPlayerHitStateCallsites{ 0x01044E87 };
        constexpr std::array<std::uintptr_t, 1> kBlockFractionCallsites{ 0x01044C65 };

        // Difficulty is applied after ReduceDamage, whenever the player is the target or the attacker.
        // The actor hit runs on the final damage and calls DoDamage, which reads the difficulty table
        // directly. DoDamage has other callers (poison, scripts) and is not given the hit, so the
        // decision is made here and scoped to this call. DoDamage's actor-hit call site belongs to
        // SCISSORS's test targets, and only the table read inside it is patched.
        // SCISSORS's death voices also wrap this call, patching it at plugin load, before this
        // plugin installs. The hook only marks the hit and calls through, so it chains behind another
        // plugin's verified trampoline here instead of refusing the whole damage model. The call passes
        // only RCX and RDX (raw disassembly 140DB1767..140DB176D).
        constexpr std::array<std::uintptr_t, 1> kActorHitCallsites{ 0x00DB176D };
        constexpr std::array<std::uintptr_t, 1> kDoDamageDifficultyCallsites{ 0x00DCA9DB };
        constexpr std::array<std::uintptr_t, 1> kActorHitEstimateCallsites{ 0x00E52D4C };

        // The same multiplier scales limb damage and the check that marks a hit fatal, which decides
        // dismemberment. Neither function is given to a call we could wrap: the projectile pipeline
        // reaches the fatal check with a tail jump, and SCISSORS owns the limb function's call
        // in the weapon pipeline. So the pipeline's own hooks leave the decision in a flag.
        constexpr std::array<std::uintptr_t, 1> kLimbDifficultyCallsites{ 0x01045979 };
        constexpr std::array<std::uintptr_t, 1> kFatalCheckDifficultyCallsites{ 0x0104618D };

        // Fire picks the weapon's override projectile, then falls back to the ammunition's own. It
        // reads the override during the call, which makes these the place to swap it for one shot.
        constexpr std::array<std::uintptr_t, 3> kFireCallsites{ 0x00DB0F24, 0x00FF2B51, 0x009BE9CE };

        // ReduceDamage's one armor curve call. The curve has other callers (AI estimates, armor display).
        constexpr std::array<std::uintptr_t, 1> kArmorCurveCallsites{ 0x01044899 };

        // The body part multiplier, which runs after the pellet divide and just before the fatal check.
        constexpr std::array<std::uintptr_t, 2> kLimbMultiplierCallsites{ 0x01042FC9, 0x0104339F };

        // Fire asks the weapon's aim model for one direction per pellet.
        constexpr std::array<std::uintptr_t, 1> kPelletSpreadCallsites{ 0x00333BD6 };

        // The dismember routine's three decisions, which roll the body part's chances and take the hit.
        constexpr std::array<std::uintptr_t, 3> kRemovalDecisionCallsites{ 0x00DEEA94, 0x00DEEDC4, 0x00DEEF3D };

        // A missile's frame update moves it by its stored velocity, then applies gravity in place. The
        // first frame takes a separate path from the muzzle, so a round leaves at full speed.
        constexpr std::array<std::uintptr_t, 1> kMissileMoveCallsites{ 0x0104874C };

        // The hit event's two power armor piece loops ask this for the damage to one piece, scale it by the
        // player or NPC setting, write the health and take the piece off at zero. Only the number is ours.
        constexpr std::array<std::uintptr_t, 2> kPieceDamageCallsites{ 0x009C3A77, 0x009CE472 };

        struct HookFamily
        {
            std::string_view name;
            std::uintptr_t targetRva;
            std::span<const std::uintptr_t> callsites;
            std::uintptr_t hook;
        };

        // Callsites another plugin already patched, and the verified trampoline each reaches. Written
        // by preflight on the game thread before install, then read-only; usually empty.
        struct ForeignCall
        {
            std::uintptr_t callsite{};
            std::uintptr_t callee{};
        };
        std::array<ForeignCall, 64> s_foreignCalls{};
        std::size_t s_foreignCallCount = 0;

        // The function a hook calls on: another plugin's detour when one held the callsite that
        // entered the hook, otherwise the native target.
        template <class Function>
        [[nodiscard]] Function through(void* returnAddress, Function native) noexcept
        {
            const auto callsite = reinterpret_cast<std::uintptr_t>(returnAddress) - 5;
            for (std::size_t i = 0; i < s_foreignCallCount; ++i) {
                if (s_foreignCalls[i].callsite == callsite) {
                    return reinterpret_cast<Function>(s_foreignCalls[i].callee);
                }
            }
            return native;
        }

        // Native targets, resolved once in install() before the first patch and then read-only. A
        // callsite another plugin patched calls on through its detour instead (see through()).
        CalcWeaponDamageFunction s_calcWeaponDamage = nullptr;
        DisplayDamageFunction s_displayDamage = nullptr;
        InitializeWeaponHitFunction s_initializeWeaponHit = nullptr;
        HitDataVoidFunction s_adjustAttackDamage = nullptr;
        HitDataFloatFunction s_criticalDamage = nullptr;
        CollectDamageFunction s_collectDamage = nullptr;
        EntryPointSixFunction s_entryPointSix = nullptr;
        EntryPointFiveFunction s_entryPointFive = nullptr;
        EntryPointFloatFunction s_entryPointFloat = nullptr;
        ReduceDamageFunction s_reduceDamage = nullptr;
        RangeFalloffFunction s_rangeFalloff = nullptr;
        PlayerHitStateFunction s_playerHitState = nullptr;
        BlockFractionFunction s_blockFraction = nullptr;
        ActorHitFunction s_actorHit = nullptr;
        DifficultyMultiplierFunction s_difficultyMultiplier = nullptr;
        DifficultyScaleFunction s_difficultyScale = nullptr;
        FireFunction s_fire = nullptr;
        ArmorCurveFunction s_armorCurve = nullptr;
        LimbMultiplierFunction s_limbMultiplier = nullptr;
        PelletSpreadFunction s_pelletSpread = nullptr;
        ConeRangeFunction s_coneRange = nullptr;
        RemovalDecisionFunction s_removalDecision = nullptr;
        MissileMoveFunction s_missileMove = nullptr;
        PieceDamageFunction s_pieceDamage = nullptr;

        // Written on the main thread from the INI and read on every hit and firing thread.
        std::atomic_bool s_damageModelActive{ true };
        std::atomic_bool s_hitTrace{ false };

        // The engine runs each hit synchronously on its calling thread, possibly several threads at
        // once, so these contexts are per thread. They never outlive the native call they annotate.
        thread_local bool s_meleeActionContext = false;
        thread_local bool s_collectingAuthoredHit = false;
        thread_local bool s_reducingAuthoredHit = false;
        thread_local bool s_reducingMeleeBlow = false;
        thread_local bool s_processingAuthoredActorHit = false;
        thread_local bool s_firingOwnedAmmo = false;

        // Set by ReduceDamage's hook for the limb multiplier that follows it on this thread.
        thread_local terminal::Outcome s_outcome{};
        thread_local float s_incomingDamage = 0.0F;

        // Not scoped: the hit pipeline's collect and reduce steps set it for the limb and fatal steps
        // that follow them on this thread, and the fatal check, the pipeline's last step, clears it.
        thread_local bool s_hitPipelineAuthored = false;

        class ScopedFlag
        {
        public:
            ScopedFlag(bool& storage, bool value) noexcept :
                _storage(storage),
                _previous(storage)
            {
                _storage = value;
            }
            ~ScopedFlag() noexcept { _storage = _previous; }
            ScopedFlag(const ScopedFlag&) = delete;
            ScopedFlag& operator=(const ScopedFlag&) = delete;

        private:
            bool& _storage;
            bool _previous;
        };

        [[nodiscard]] const InstanceData* resolveInstanceData(const WeaponInstance& weapon) noexcept
        {
            if (weapon.instanceData) {
                return static_cast<const InstanceData*>(weapon.instanceData.get());
            }
            const auto* object = weapon.object ? weapon.object->As<RE::TESObjectWEAP>() : nullptr;
            return object ? &object->weaponData : nullptr;
        }

        // Every hook reaches the table through these two, so the bDamageModel off switch covers all of them.
        [[nodiscard]] const ammo_table::Entry* findOwnedAmmo(const RE::TESAmmo* ammo) noexcept
        {
            return s_damageModelActive.load(std::memory_order_relaxed) ? ammo_table::find(ammo) : nullptr;
        }

        // Actor fire can pass a null ammo argument while the weapon instance owns the loaded ammunition.
        [[nodiscard]] const ammo_table::Entry* findEffectiveAmmo(const InstanceData* instance, const RE::TESAmmo* ammo) noexcept
        {
            return findOwnedAmmo(ammo ? ammo : (instance ? instance->ammo : nullptr));
        }

        // The blow of a melee hit, or null when it is not one the model owns. A hit with no weapon form stays native.
        [[nodiscard]] const melee::Blow* meleeBlowOf(const RE::HitData& hit) noexcept
        {
            const auto flags = hit.flags.underlying();
            if (!s_damageModelActive.load(std::memory_order_relaxed) || !damage_policy::isMeleeBlow(flags)) {
                return nullptr;
            }
            const auto* weapon = hit.weapon.object ? hit.weapon.object->As<RE::TESObjectWEAP>() : nullptr;
            if (!weapon) {
                return nullptr;
            }
            const bool bash = (flags & damage_policy::kBashFlag) != 0 || damage_policy::isRangedWeaponType(weapon->weaponData.type.underlying());
            return &melee::classify(*weapon, hit.weapon.instanceData.get(), bash);
        }

        [[nodiscard]] bool isAuthoredHit(const RE::HitData* hit) noexcept
        {
            if (!hit) {
                return false;
            }
            if (meleeBlowOf(*hit)) {
                return true;
            }
            const auto* instance = resolveInstanceData(hit->weapon);
            return instance && damage_policy::isAuthoredHit(
                                   hit->flags.underlying(),
                                   instance->type.underlying(),
                                   findEffectiveAmmo(instance, hit->ammo) != nullptr);
        }

        [[nodiscard]] const terminal::Caliber* caliberOfHit(const RE::HitData& hit) noexcept;
        [[nodiscard]] float shotDistance(const RE::HitData& hit) noexcept;
        [[nodiscard]] terminal::Caliber atImpact(const RE::HitData& hit, const terminal::Caliber& caliber) noexcept;

        // Rate-limited because a burst of pellets can produce many hits in one frame. The counters
        // are shared across hit threads; a lost update only moves the limit by a line.
        void traceHit(const RE::HitData& hit, bool authored, float incoming, const terminal::Outcome& outcome) noexcept
        {
            static std::atomic_uint64_t s_second{ 0 };
            static std::atomic_uint32_t s_linesThisSecond{ 0 };
            constexpr std::uint32_t kMaxLinesPerSecond = 40;

            const auto flags = hit.flags.underlying();
            const auto second = GetTickCount64() / 1000;
            if (s_second.exchange(second, std::memory_order_relaxed) != second) {
                s_linesThisSecond.store(0, std::memory_order_relaxed);
            }
            if (s_linesThisSecond.fetch_add(1, std::memory_order_relaxed) >= kMaxLinesPerSecond) {
                return;
            }
            const auto* instance = resolveInstanceData(hit.weapon);
            const auto* ammo = hit.ammo ? hit.ammo : (instance ? instance->ammo : nullptr);
            const auto target = hit.target.get();
            const auto* caliber = meleeBlowOf(hit) ? nullptr : caliberOfHit(hit);
            constexpr float kGameUnitsPerMetre = 70.0F;
            const float distance = shotDistance(hit) / kGameUnitsPerMetre;
            const float energyKept = caliber ? atImpact(hit, *caliber).energyKept : 1.0F;
            spdlog::info(
                "Hit: target={:08X} ammo={:08X} authored={} flags={:08X} limb={} distance={:.1f}m energyKept={:.3f} incoming={:.1f} afterReduction={:.1f} resolved={} level={} stopped={} kill={} armor={:.3f} limbFactor={:.2f} weapon={:08X}",
                target ? target->GetFormID() : 0u,
                ammo ? ammo->GetFormID() : 0u,
                authored,
                static_cast<std::uint32_t>(flags),
                hit.damageLimb.underlying(),
                distance,
                energyKept,
                incoming,
                hit.healthDamage,
                outcome.active,
                outcome.armorLevel,
                outcome.stopped,
                outcome.instantKill,
                outcome.armorFraction,
                outcome.limbFactor,
                hit.weapon.object ? hit.weapon.object->GetFormID() : 0u);
        }

        void reportHit(const RE::HitData& hit, bool authored, float incoming, const terminal::Outcome& outcome) noexcept
        {
            if (damage_policy::isPrediction(hit.flags.underlying())) {
                return;
            }
            if (s_hitTrace.load(std::memory_order_relaxed)) {
                traceHit(hit, authored, incoming, outcome);
            }
            const auto& location = hit.impactData.location;
            const float point[3]{ location.x, location.y, location.z };
            hit_labels::record(point, authored, incoming, hit.healthDamage);
        }

        [[nodiscard]] const terminal::Caliber* caliberOfHit(const RE::HitData& hit) noexcept
        {
            const auto* entry = findEffectiveAmmo(resolveInstanceData(hit.weapon), hit.ammo);
            return entry ? entry->caliber : nullptr;
        }

        [[nodiscard]] std::uint32_t pelletsOfHit(const RE::HitData& hit) noexcept
        {
            const auto* instance = resolveInstanceData(hit.weapon);
            const auto* ranged = instance ? instance->rangedData : nullptr;
            return ranged && ranged->numProjectiles > 1 ? static_cast<std::uint32_t>(ranged->numProjectiles) : 1u;
        }

        // From the aggressor to the impact, in game units; negative when the hit names no aggressor.
        [[nodiscard]] float shotDistance(const RE::HitData& hit) noexcept
        {
            const auto aggressor = hit.aggressor.get();
            if (!aggressor) {
                return -1.0F;
            }
            const auto& from = aggressor->data.location;
            const auto& to = hit.impactData.location;
            const float x = to.x - from.x;
            const float y = to.y - from.y;
            const float z = to.z - from.z;
            return std::sqrt(x * x + y * y + z * z);
        }

        // The round as it reached the target. Armor reads the energy it kept for what gets through and how much a
        // suit piece wears; the round's class (stop level, crossing a suit) stays its muzzle class.
        [[nodiscard]] terminal::Caliber atImpact(const RE::HitData& hit, const terminal::Caliber& caliber) noexcept
        {
            return presets::atDistance(caliber, shotDistance(hit));
        }

        [[nodiscard]] terminal::Outcome resolveOutcome(const RE::HitData& hit) noexcept
        {
            if (const auto* blow = meleeBlowOf(hit)) {
                const auto target = hit.target.get();
                return target ? terminal::resolveMelee(*target, *blow->protection, hit.damageLimb.underlying()) : terminal::Outcome{};
            }
            const auto* caliber = caliberOfHit(hit);
            if (!caliber) {
                return {};
            }
            const auto target = hit.target.get();
            return target ? terminal::resolve(*target, atImpact(hit, *caliber), hit.damageLimb.underlying(), pelletsOfHit(hit)) : terminal::Outcome{};
        }

        float calcWeaponDamageHook(RE::TESForm* attacker, InstanceData* instance, RE::TESAmmo* ammo, float condition, float range) noexcept
        {
            const auto original = through(_ReturnAddress(), s_calcWeaponDamage);
            if (instance && !s_meleeActionContext) {
                const auto* entry = findEffectiveAmmo(instance, ammo);
                if (damage_policy::usesAuthoredDamage(instance->type.underlying(), entry != nullptr, false)) {
                    return entry->damage;
                }
            }
            return original(attacker, instance, ammo, condition, range);
        }

        float displayDamageHook(WeaponInstance* weapon, RE::TESAmmo* ammo, float condition) noexcept
        {
            const auto original = through(_ReturnAddress(), s_displayDamage);
            const auto* instance = weapon ? resolveInstanceData(*weapon) : nullptr;
            const auto* object = weapon && weapon->object ? weapon->object->As<RE::TESObjectWEAP>() : nullptr;
            if (instance && object && s_damageModelActive.load(std::memory_order_relaxed) && instance->type.underlying() < damage_policy::kBow) {
                const float damage = melee::damageOf(melee::classify(*object, weapon->instanceData.get(), false), *object, weapon->instanceData.get());
                if (damage > 0.0F) {
                    return damage;
                }
            }
            if (instance) {
                const auto* entry = findEffectiveAmmo(instance, ammo);
                if (damage_policy::usesAuthoredDamage(instance->type.underlying(), entry != nullptr, false)) {
                    return entry->damage;
                }
            }
            return original(weapon, ammo, condition);
        }

        void initializeWeaponHitHook(RE::HitData* hit, RE::Actor* target, RE::Actor* aggressor, WeaponInstance* weapon, std::uint32_t equipIndex, bool meleeAction) noexcept
        {
            const auto original = through(_ReturnAddress(), s_initializeWeaponHit);
            ScopedFlag scope(s_meleeActionContext, s_meleeActionContext || meleeAction);
            original(hit, target, aggressor, weapon, equipIndex, meleeAction);
        }

        // The native function scales a hit by the weapon's range stats; distance is in game units (it is compared
        // with the weapon's min and max range at 0x14064FE50). Our rounds instead keep the share of their damage
        // that the energy left after the same air drag carries. A blow keeps full damage.
        float rangeFalloffHook(RE::TESForm* attacker, WeaponInstance* weapon, float distance) noexcept
        {
            const auto original = through(_ReturnAddress(), s_rangeFalloff);
            const auto* instance = weapon ? resolveInstanceData(*weapon) : nullptr;
            const auto* entry = instance ? findEffectiveAmmo(instance, nullptr) : nullptr;
            if (instance && damage_policy::usesAuthoredDamage(instance->type.underlying(), entry != nullptr, s_meleeActionContext)) {
                if (entry && entry->caliber && !s_meleeActionContext) {
                    return terminal::damageShare(presets::atDistance(*entry->caliber, distance));
                }
                return 1.0F;
            }
            return original(attacker, weapon, distance);
        }

        void adjustAttackDamageHook(RE::HitData* hit) noexcept
        {
            const auto original = through(_ReturnAddress(), s_adjustAttackDamage);
            if (!isAuthoredHit(hit)) {
                original(hit);
            }
        }

        // The melee builder calls this straight after the weapon formula has stored the hit's damage, with the
        // weapon on the hit, and reads the damage back afterwards. The formula hook sees only the instance data,
        // which cannot name an unarmed weapon. Attack multipliers and SCISSORS's swing scale apply later.
        float criticalDamageHook(RE::HitData* hit) noexcept
        {
            const auto original = through(_ReturnAddress(), s_criticalDamage);
            if (const auto* blow = hit ? meleeBlowOf(*hit) : nullptr) {
                const auto* weapon = hit->weapon.object->As<RE::TESObjectWEAP>();
                const float damage = melee::damageOf(*blow, *weapon, hit->weapon.instanceData.get());
                if (damage > 0.0F) {
                    hit->healthDamage = damage;
                }
                return 0.0F;
            }
            return isAuthoredHit(hit) ? 0.0F : original(hit);
        }

        void collectDamageHook(RE::HitData* hit, WeaponInstance* weapon, float power) noexcept
        {
            const auto original = through(_ReturnAddress(), s_collectDamage);
            const bool authored = isAuthoredHit(hit);
            s_hitPipelineAuthored = authored;
            ScopedFlag scope(s_collectingAuthoredHit, authored);
            original(hit, weapon, power);
        }

        void typedEntryPointHook(std::uint32_t entryPoint, RE::Actor* perkOwner, void* argument1, void* argument2, void* argument3, void* argument4) noexcept
        {
            const auto original = through(_ReturnAddress(), s_entryPointSix);
            if (!s_collectingAuthoredHit) {
                original(entryPoint, perkOwner, argument1, argument2, argument3, argument4);
            }
        }

        void reduceDamageHook(RE::HitData* hit, bool onlyPhysical) noexcept
        {
            const auto original = through(_ReturnAddress(), s_reduceDamage);
            const bool authored = isAuthoredHit(hit);
            const float incoming = hit ? hit->healthDamage : 0.0F;
            s_hitPipelineAuthored = authored;
            s_outcome = authored ? resolveOutcome(*hit) : terminal::Outcome{};
            s_incomingDamage = incoming;
            {
                ScopedFlag scope(s_reducingAuthoredHit, authored);
                ScopedFlag melee(s_reducingMeleeBlow, authored && meleeBlowOf(*hit));
                original(hit, onlyPhysical);
            }
            // A resolved hit is reported by the limb multiplier, once its damage is final.
            if (hit && !s_outcome.active) {
                reportHit(*hit, authored, incoming, s_outcome);
            }
        }

        float armorCurveHook(void* table, float damage, float resist) noexcept
        {
            const auto original = through(_ReturnAddress(), s_armorCurve);
            return s_reducingAuthoredHit && s_outcome.active ? s_outcome.armorFraction : original(table, damage, resist);
        }

        void limbMultiplierHook(RE::HitData* hit) noexcept
        {
            const auto original = through(_ReturnAddress(), s_limbMultiplier);
            const auto outcome = std::exchange(s_outcome, terminal::Outcome{});
            if (!hit || !outcome.active) {
                original(hit);
                return;
            }
            hit->healthDamage *= outcome.limbFactor;
            hit->targetedLimbDamage *= outcome.armorFraction * outcome.limbFactor;
            // The suit's belly reports no limb, and the hit event skips the piece damage for that. It is the
            // torso piece the round struck, so name it for the event that reads this hit next.
            if (outcome.armorLevel == terminal::kPlateLevel && hit->damageLimb.underlying() == kNoLimb) {
                hit->damageLimb = static_cast<RE::BGSBodyPartDefs::LIMB_ENUM>(kTorsoLimb);
            }
            if (outcome.instantKill && !damage_policy::isPrediction(hit->flags.underlying())) {
                // Current health plus one, not a large constant: SCISSORS sizes shot impacts by hit damage.
                if (const auto target = hit->target.get()) {
                    const auto* values = RE::ActorValue::GetSingleton();
                    const float health = values && values->health ? target->GetActorValue(*values->health) : 0.0F;
                    hit->healthDamage = std::max(hit->healthDamage, health + 1.0F);
                }
            }
            // Actor hit processing (0xE5293B) skips the hit's reaction when health and limb damage are both zero,
            // and calls the impact handler (0xE54362, which plays the material's effect, decal and sound) only above
            // zero. A round the armor fully held still struck, so it keeps a token number: 1000 such hits cost 1 health.
            constexpr float kImpactOnlyDamage = 0.001F;
            if (hit->healthDamage < kImpactOnlyDamage && !damage_policy::isPrediction(hit->flags.underlying())) {
                hit->healthDamage = kImpactOnlyDamage;
            }
            reportHit(*hit, true, s_incomingDamage, outcome);
        }

        // The decision only runs for a hit the engine is already dismembering, so a hit that does not
        // kill removes nothing. Sets both answers from the table and, when one is wanted, still runs the
        // native function, which refuses a part already removed or a body part that cannot be.
        void removalDecisionHook(RE::Actor* actor, void* part, RE::HitData* hit, WeaponInstance* weapon, bool flag, bool* dismember, bool* explode) noexcept
        {
            const auto original = through(_ReturnAddress(), s_removalDecision);
            // A melee kill keeps the game's rolls, unless armor held the blow: nothing comes off through it.
            if (hit && meleeBlowOf(*hit)) {
                if (resolveOutcome(*hit).stopped && dismember && explode) {
                    *dismember = false;
                    *explode = false;
                    return;
                }
                original(actor, part, hit, weapon, flag, dismember, explode);
                return;
            }
            const auto* caliber = hit && isAuthoredHit(hit) ? caliberOfHit(*hit) : nullptr;
            const auto impact = caliber ? atImpact(*hit, *caliber) : terminal::Caliber{};
            const auto outcome = caliber && actor ? terminal::resolve(*actor, impact, hit->damageLimb.underlying(), pelletsOfHit(*hit)) : terminal::Outcome{};
            if (!outcome.active || outcome.vanillaRemoval || !part || !dismember || !explode) {
                original(actor, part, hit, weapon, flag, dismember, explode);
                return;
            }

            // With no aggressor, nothing is close enough for a range-limited burst.
            const float measured = shotDistance(*hit);
            const float distance = measured < 0.0F ? std::numeric_limits<float>::max() : measured;
            const auto partLimb = *(static_cast<const std::uint8_t*>(part) + kBodyPartLimb);
            const auto partFlags = *(static_cast<const std::uint8_t*>(part) + kBodyPartFlags);
            const auto wanted = terminal::removal(impact, outcome, hit->damageLimb.underlying(), partLimb, partFlags, distance);

            *dismember = wanted.sever;
            *explode = wanted.burst;
            if (wanted.sever || wanted.burst) {
                original(actor, part, hit, weapon, flag, dismember, explode);
                *dismember = *dismember && wanted.sever;
                *explode = *explode && wanted.burst;
            }
        }

        // Replaces the aim model's cone for a pellet weapon firing our ammunition. The cone multiplier is
        // the only input the native function scales by, so dividing by its own cone sets the result.
        void pelletSpreadHook(void* aimModel, float yaw, float pitch, float multiplier, void* output) noexcept
        {
            const auto original = through(_ReturnAddress(), s_pelletSpread);
            if (s_firingOwnedAmmo && aimModel && output) {
                const auto count = *reinterpret_cast<const std::uint32_t*>(static_cast<const std::byte*>(output) + kSpreadOutputCount);
                if (count > 1) {
                    float maximum = 0.0F;
                    float minimum = 0.0F;
                    s_coneRange(aimModel, &maximum, &minimum);
                    const float blend = *reinterpret_cast<const float*>(static_cast<const std::byte*>(aimModel) + kAimModelConeBlend);
                    const float cone = (maximum - minimum) * blend + minimum;
                    if (cone > 0.0F) {
                        multiplier = terminal::shotgunConeRadians() / cone;
                    }
                    if (s_hitTrace.load(std::memory_order_relaxed)) {
                        spdlog::info("Pellets: {} over native cone {:.4f} (min {:.4f}, max {:.4f}), multiplier {:.4f}", count, cone, minimum, maximum, multiplier);
                    }
                }
            }
            original(aimModel, yaw, pitch, multiplier, output);
        }

        float pieceDamageHook(RE::Actor* wearer, RE::HitData* hit, RE::TESForm* piece, RE::ExtraDataList* extra) noexcept
        {
            const auto original = through(_ReturnAddress(), s_pieceDamage);
            const auto* blow = hit ? meleeBlowOf(*hit) : nullptr;
            const auto* caliber = !blow && hit && isAuthoredHit(hit) ? caliberOfHit(*hit) : nullptr;
            if (!blow && !caliber) {
                return original(wearer, hit, piece, extra);
            }
            const float damage = blow
                ? terminal::pieceDamage(melee::pieceWear(*blow, *hit->weapon.object->As<RE::TESObjectWEAP>(), hit->weapon.instanceData.get(), hit->physicalDamage))
                : terminal::pieceDamage(atImpact(*hit, *caliber), pelletsOfHit(*hit));
            if (s_hitTrace.load(std::memory_order_relaxed)) {
                spdlog::info("Piece: wearer={:08X} piece={:08X} damage={:.2f}", wearer ? wearer->GetFormID() : 0u, piece ? piece->GetFormID() : 0u, damage);
            }
            return damage;
        }

        // Air drag on our rounds, which changes drop and time of flight. Damage never reads this speed: the
        // range falloff and atImpact() apply the same drag over the distance between shooter and target.
        void missileMoveHook(RE::Projectile* projectile, float delta) noexcept
        {
            const auto original = through(_ReturnAddress(), s_missileMove);
            const float factor = projectile && delta > 0.0F ? presets::dragFactorOf(projectile->GetObjectReference()) : 0.0F;
            if (factor > 0.0F) {
                auto& velocity = projectile->velocity;
                const float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
                if (speed > 0.0F) {
                    const float scale = presets::speedAfterDrag(speed, factor, delta) / speed;
                    velocity.x *= scale;
                    velocity.y *= scale;
                    velocity.z *= scale;
                }
            }
            original(projectile, delta);
        }

        void reduceFiveEntryPointHook(std::uint32_t entryPoint, RE::Actor* perkOwner, void* argument1, void* argument2, void* argument3) noexcept
        {
            const auto original = through(_ReturnAddress(), s_entryPointFive);
            if (!s_reducingAuthoredHit) {
                original(entryPoint, perkOwner, argument1, argument2, argument3);
            }
        }

        void reduceFloatEntryPointHook(std::uint32_t entryPoint, RE::Actor* perkOwner, float* value) noexcept
        {
            const auto original = through(_ReturnAddress(), s_entryPointFloat);
            if (!s_reducingAuthoredHit) {
                original(entryPoint, perkOwner, value);
            }
        }

        float playerHitStateHook(void* state) noexcept
        {
            const auto original = through(_ReturnAddress(), s_playerHitState);
            return s_reducingAuthoredHit ? 1.0F : original(state);
        }

        // Zero leaves the hit unblocked: ReduceDamage only reduces by a fraction above zero, and the
        // blocking actor still gets the blocked flag and its reaction. A melee blow can really be blocked.
        float blockFractionHook(float power, float firstStat, float secondStat, bool mode1, bool mode2, bool mode3) noexcept
        {
            const auto original = through(_ReturnAddress(), s_blockFraction);
            return s_reducingAuthoredHit && !s_reducingMeleeBlow ? 0.0F : original(power, firstStat, secondStat, mode1, mode2, mode3);
        }

        void actorHitHook(RE::Actor* target, RE::HitData* hit) noexcept
        {
            const auto original = through(_ReturnAddress(), s_actorHit);
            ScopedFlag scope(s_processingAuthoredActorHit, isAuthoredHit(hit));
            original(target, hit);
        }

        float doDamageDifficultyHook(std::int32_t difficulty, void* healthValue, bool targetIsPlayer) noexcept
        {
            const auto original = through(_ReturnAddress(), s_difficultyMultiplier);
            return s_processingAuthoredActorHit ? 1.0F : original(difficulty, healthValue, targetIsPlayer);
        }

        float actorHitEstimateDifficultyHook(RE::Actor* target, float damage) noexcept
        {
            const auto original = through(_ReturnAddress(), s_difficultyScale);
            return s_processingAuthoredActorHit ? damage : original(target, damage);
        }

        float limbDifficultyHook(RE::Actor* target, float damage) noexcept
        {
            const auto original = through(_ReturnAddress(), s_difficultyScale);
            return s_hitPipelineAuthored ? damage : original(target, damage);
        }

        float fatalCheckDifficultyHook(RE::Actor* target, float damage) noexcept
        {
            const auto original = through(_ReturnAddress(), s_difficultyScale);
            const bool authored = s_hitPipelineAuthored;
            s_hitPipelineAuthored = false;
            return authored ? damage : original(target, damage);
        }

        // The projectile this shot should use, or null to leave the weapon's own choice in place.
        // With no override and no suppressor, Fire already picks the ammunition's normal projectile.
        [[nodiscard]] RE::BGSProjectile* chooseProjectile(const ammo_table::Entry& entry, const InstanceData& instance, const RE::BGSProjectile* current) noexcept
        {
            constexpr std::int32_t kSoundLevelSilent = 2;
            constexpr std::int32_t kSoundLevelQuiet = 4;

            if (current && (current == entry.normal || current == entry.suppressed || !ammo_table::isReplaceable(*current))) {
                return nullptr;
            }
            const auto weaponLevel = instance.soundLevel.underlying();
            const bool suppressed = weaponLevel == kSoundLevelSilent || weaponLevel == kSoundLevelQuiet ||
                                    (current && ammo_table::isSuppressed(*current));
            if (suppressed && entry.suppressed) {
                return entry.suppressed;
            }
            return current ? entry.normal : nullptr;
        }

        // Holds the swap for exactly one native Fire call.
        class ScopedProjectileOverride
        {
        public:
            ScopedProjectileOverride(RangedData& ranged, RE::BGSProjectile* replacement) noexcept :
                _ranged(ranged),
                _previous(ranged.overrideProjectile)
            {
                _ranged.overrideProjectile = replacement;
            }
            ~ScopedProjectileOverride() noexcept { _ranged.overrideProjectile = _previous; }
            ScopedProjectileOverride(const ScopedProjectileOverride&) = delete;
            ScopedProjectileOverride& operator=(const ScopedProjectileOverride&) = delete;

        private:
            RangedData& _ranged;
            RE::BGSProjectile* _previous;
        };

        // One line per Fire call, so a trigger pull that fired can be told from a shot that missed, and the
        // log shows which flight model and impact a provider actually gave the round.
        void traceShot(const RE::TESAmmo* ammo, const RangedData* ranged, const RE::BGSProjectile* replacement) noexcept
        {
            const RE::BGSProjectile* projectile = replacement;
            if (!projectile && ranged) {
                projectile = ranged->overrideProjectile;
            }
            if (!projectile && ammo) {
                projectile = ammo->data.projectile;
            }
            if (!projectile) {
                spdlog::info("Shot: ammo={:08X} has no projectile", ammo ? ammo->GetFormID() : 0u);
                return;
            }
            const auto* explosion = projectile->data.explosionType;
            spdlog::info(
                "Shot: ammo={:08X} box={} casing={} projectile={:08X} model={} flags={:04X} speed={:.0f} lifetime={:.2f} timer={:.2f} explosion={:08X}",
                ammo ? ammo->GetFormID() : 0u,
                ammo ? static_cast<const RE::TESModel&>(*ammo).model.c_str() : "",
                ammo ? ammo->shellCasing.model.c_str() : "",
                projectile->GetFormID(),
                static_cast<const RE::TESModel&>(*projectile).model.c_str(),
                projectile->data.flags,
                projectile->data.speed,
                projectile->data.lifetime,
                projectile->data.explosionTimer,
                explosion ? explosion->GetFormID() : 0u);
        }

        void fireHook(WeaponInstance* weapon, RE::TESObjectREFR* shooter, std::uint32_t equipIndex, RE::TESAmmo* ammo, RE::AlchemyItem* poison) noexcept
        {
            const auto original = through(_ReturnAddress(), s_fire);
            const auto* resolvedInstance = weapon ? resolveInstanceData(*weapon) : nullptr;
            ScopedFlag firing(s_firingOwnedAmmo, findEffectiveAmmo(resolvedInstance, ammo) != nullptr);

            // Only a per-instance RangedData is swapped. A base record's RangedData is shared by every
            // actor holding that weapon, so another thread could observe or restore the swap.
            auto* instance = weapon && weapon->instanceData ? static_cast<InstanceData*>(weapon->instanceData.get()) : nullptr;
            auto* ranged = instance ? instance->rangedData : nullptr;
            const auto* entry = ranged ? findEffectiveAmmo(instance, ammo) : nullptr;
            auto* replacement = entry ? chooseProjectile(*entry, *instance, ranged->overrideProjectile) : nullptr;
            if (s_hitTrace.load(std::memory_order_relaxed)) {
                traceShot(entry ? entry->ammo : ammo, ranged, replacement);  // Fire is usually passed no ammunition.
            }
            if (!replacement) {
                original(weapon, shooter, equipIndex, ammo, poison);
                return;
            }
            ScopedProjectileOverride scope(*ranged, replacement);
            original(weapon, shooter, equipIndex, ammo, poison);
        }

        [[nodiscard]] std::array<HookFamily, 25> hookFamilies() noexcept
        {
            const auto address = [](auto hook) { return reinterpret_cast<std::uintptr_t>(hook); };
            return {
                HookFamily{ "weapon formula", kCalcWeaponDamageTarget, kCalcWeaponDamageCallsites, address(&calcWeaponDamageHook) },
                HookFamily{ "weapon display", kDisplayDamageTarget, kDisplayDamageCallsites, address(&displayDamageHook) },
                HookFamily{ "hit initialization", kInitializeWeaponHitTarget, kInitializeWeaponHitCallsites, address(&initializeWeaponHitHook) },
                HookFamily{ "attack adjustment", kAdjustAttackDamageTarget, kAdjustAttackDamageCallsites, address(&adjustAttackDamageHook) },
                HookFamily{ "critical damage", kCriticalDamageTarget, kCriticalDamageCallsites, address(&criticalDamageHook) },
                HookFamily{ "typed damage", kCollectDamageTarget, kCollectDamageCallsites, address(&collectDamageHook) },
                HookFamily{ "typed damage perks", kEntryPointTarget, kTypedEntryPointCallsites, address(&typedEntryPointHook) },
                HookFamily{ "damage reduction", kReduceDamageTarget, kReduceDamageCallsites, address(&reduceDamageHook) },
                HookFamily{ "reduction perks", kEntryPointTarget, kReduceFiveEntryPointCallsites, address(&reduceFiveEntryPointHook) },
                HookFamily{ "blocked percentage perk", kEntryPointTarget, kReduceFloatEntryPointCallsites, address(&reduceFloatEntryPointHook) },
                HookFamily{ "range falloff", kRangeFalloffTarget, kRangeFalloffCallsites, address(&rangeFalloffHook) },
                HookFamily{ "player hit state", kPlayerHitStateTarget, kPlayerHitStateCallsites, address(&playerHitStateHook) },
                HookFamily{ "block fraction", kBlockFractionTarget, kBlockFractionCallsites, address(&blockFractionHook) },
                HookFamily{ "actor hit", kActorHitTarget, kActorHitCallsites, address(&actorHitHook) },
                HookFamily{ "health difficulty", kDifficultyMultiplierTarget, kDoDamageDifficultyCallsites, address(&doDamageDifficultyHook) },
                HookFamily{ "actor hit estimate difficulty", kDifficultyScaleTarget, kActorHitEstimateCallsites, address(&actorHitEstimateDifficultyHook) },
                HookFamily{ "limb difficulty", kDifficultyScaleTarget, kLimbDifficultyCallsites, address(&limbDifficultyHook) },
                HookFamily{ "fatal check difficulty", kDifficultyScaleTarget, kFatalCheckDifficultyCallsites, address(&fatalCheckDifficultyHook) },
                HookFamily{ "weapon fire", kFireTarget, kFireCallsites, address(&fireHook) },
                HookFamily{ "armor curve", kArmorCurveTarget, kArmorCurveCallsites, address(&armorCurveHook) },
                HookFamily{ "limb multiplier", kLimbMultiplierTarget, kLimbMultiplierCallsites, address(&limbMultiplierHook) },
                HookFamily{ "pellet spread", kPelletSpreadTarget, kPelletSpreadCallsites, address(&pelletSpreadHook) },
                HookFamily{ "removal decision", kRemovalDecisionTarget, kRemovalDecisionCallsites, address(&removalDecisionHook) },
                HookFamily{ "missile move", kMissileMoveTarget, kMissileMoveCallsites, address(&missileMoveHook) },
                HookFamily{ "power armor piece damage", kPieceDamageTarget, kPieceDamageCallsites, address(&pieceDamageHook) },
            };
        }

        [[nodiscard]] std::optional<std::uintptr_t> decodeCallTarget(std::uintptr_t callsite) noexcept
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(callsite);
            if (bytes[0] != 0xE8) {
                return std::nullopt;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, bytes + 1, sizeof(displacement));
            return callsite + 5 + displacement;
        }

        template <class Function>
        [[nodiscard]] Function nativeFunction(std::uintptr_t base, std::uintptr_t rva) noexcept
        {
            return reinterpret_cast<Function>(base + rva);
        }

        // Another plugin's F4SE call patch: the call reaches its trampoline stub, `jmp [rip+0]` and the
        // absolute address of a function inside a loaded DLL other than the game. Load time only.
        [[nodiscard]] std::optional<std::wstring> foreignStubOwner(std::uintptr_t stub) noexcept
        {
            constexpr std::size_t kStubBytes = 14;
            constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(stub), &region, sizeof(region)) || region.State != MEM_COMMIT ||
                (region.Protect & kExecutable) == 0 || (region.Protect & PAGE_GUARD) != 0 ||
                stub + kStubBytes > reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize) {
                return std::nullopt;
            }
            constexpr std::array<std::uint8_t, 6> kJump{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(stub);
            if (std::memcmp(bytes, kJump.data(), kJump.size()) != 0) {
                return std::nullopt;
            }
            std::uintptr_t destination{};
            std::memcpy(&destination, bytes + kJump.size(), sizeof(destination));
            HMODULE owner = nullptr;
            HMODULE self = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(destination), &owner) ||
                !owner || owner == GetModuleHandleW(nullptr) ||
                (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&foreignStubOwner), &self) &&
                    owner == self)) {
                return std::nullopt;
            }
            std::array<wchar_t, MAX_PATH> path{};
            const auto length = GetModuleFileNameW(owner, path.data(), static_cast<DWORD>(path.size()));
            return std::filesystem::path(std::wstring(path.data(), length)).filename().wstring();
        }
    }

    bool preflight() noexcept
    {
        const auto base = REL::Module::get().base();
        s_foreignCallCount = 0;
        for (const auto& family : hookFamilies()) {
            for (const auto rva : family.callsites) {
                const auto address = base + rva;
                const auto target = decodeCallTarget(address);
                const auto expected = base + family.targetRva;
                // Another plugin's verified F4SE patch stays in place: the hook calls through it.
                if (target && *target != expected && s_foreignCallCount < s_foreignCalls.size()) {
                    if (const auto owner = foreignStubOwner(*target)) {
                        s_foreignCalls[s_foreignCallCount++] = { address, *target };
                        spdlog::info("The {} callsite at RVA {:X} is already patched by {}; this hook calls through it",
                            family.name, rva, std::filesystem::path(*owner).string());
                        continue;
                    }
                }
                if (!target || *target != expected) {
                    spdlog::critical(
                        "Preflight rejected {} callsite at RVA {:X}: it calls {:X}, expected {:X}. Another plugin may patch it.",
                        family.name,
                        rva,
                        target.value_or(0),
                        expected);
                    return false;
                }
            }
        }
        const auto coneCall = decodeCallTarget(base + kConeRangeCallInSpread);
        if (!coneCall || *coneCall != base + kConeRangeTarget) {
            spdlog::critical("Preflight rejected the cone range call at RVA {:X}", kConeRangeCallInSpread);
            return false;
        }
        if (std::memcmp(reinterpret_cast<const void*>(base + kActorInPowerArmorTarget), kActorInPowerArmorBytes.data(), kActorInPowerArmorBytes.size()) != 0) {
            spdlog::critical("Preflight rejected the power armor test at RVA {:X}", kActorInPowerArmorTarget);
            return false;
        }
        return true;
    }

    void setDamageModelActive(bool active) noexcept
    {
        s_damageModelActive.store(active, std::memory_order_relaxed);
    }

    void setHitTrace(bool enabled) noexcept
    {
        s_hitTrace.store(enabled, std::memory_order_relaxed);
    }

    bool install() noexcept
    {
        // F4SE's trampoline spends 14 bytes on each 5-byte call patch.
        constexpr std::size_t kTrampolineBytesPerPatch = 14;

        try {
            const auto base = REL::Module::get().base();
            const auto families = hookFamilies();
            std::size_t patchCount = 0;
            for (const auto& family : families) {
                patchCount += family.callsites.size();
            }
            auto& trampoline = F4SE::GetTrampoline();
            if (trampoline.free_size() < patchCount * kTrampolineBytesPerPatch) {
                spdlog::critical("Trampoline has {} free bytes for {} patches; nothing was patched", trampoline.free_size(), patchCount);
                return false;
            }

            s_calcWeaponDamage = nativeFunction<CalcWeaponDamageFunction>(base, kCalcWeaponDamageTarget);
            s_displayDamage = nativeFunction<DisplayDamageFunction>(base, kDisplayDamageTarget);
            s_initializeWeaponHit = nativeFunction<InitializeWeaponHitFunction>(base, kInitializeWeaponHitTarget);
            s_adjustAttackDamage = nativeFunction<HitDataVoidFunction>(base, kAdjustAttackDamageTarget);
            s_criticalDamage = nativeFunction<HitDataFloatFunction>(base, kCriticalDamageTarget);
            s_collectDamage = nativeFunction<CollectDamageFunction>(base, kCollectDamageTarget);
            s_entryPointSix = nativeFunction<EntryPointSixFunction>(base, kEntryPointTarget);
            s_entryPointFive = nativeFunction<EntryPointFiveFunction>(base, kEntryPointTarget);
            s_entryPointFloat = nativeFunction<EntryPointFloatFunction>(base, kEntryPointTarget);
            s_reduceDamage = nativeFunction<ReduceDamageFunction>(base, kReduceDamageTarget);
            s_rangeFalloff = nativeFunction<RangeFalloffFunction>(base, kRangeFalloffTarget);
            s_playerHitState = nativeFunction<PlayerHitStateFunction>(base, kPlayerHitStateTarget);
            s_blockFraction = nativeFunction<BlockFractionFunction>(base, kBlockFractionTarget);
            s_actorHit = nativeFunction<ActorHitFunction>(base, kActorHitTarget);
            s_difficultyMultiplier = nativeFunction<DifficultyMultiplierFunction>(base, kDifficultyMultiplierTarget);
            s_difficultyScale = nativeFunction<DifficultyScaleFunction>(base, kDifficultyScaleTarget);
            s_fire = nativeFunction<FireFunction>(base, kFireTarget);
            s_armorCurve = nativeFunction<ArmorCurveFunction>(base, kArmorCurveTarget);
            s_limbMultiplier = nativeFunction<LimbMultiplierFunction>(base, kLimbMultiplierTarget);
            s_pelletSpread = nativeFunction<PelletSpreadFunction>(base, kPelletSpreadTarget);
            s_coneRange = nativeFunction<ConeRangeFunction>(base, kConeRangeTarget);
            s_removalDecision = nativeFunction<RemovalDecisionFunction>(base, kRemovalDecisionTarget);
            s_missileMove = nativeFunction<MissileMoveFunction>(base, kMissileMoveTarget);
            s_pieceDamage = nativeFunction<PieceDamageFunction>(base, kPieceDamageTarget);

            for (const auto& family : families) {
                for (const auto rva : family.callsites) {
                    trampoline.write_call<5>(base + rva, family.hook);
                }
            }
            spdlog::info("Installed {} hooks", patchCount);
            return true;
        } catch (const std::exception& error) {
            spdlog::critical("Hook installation failed: {}", error.what());
        } catch (...) {
            spdlog::critical("Hook installation failed with an unknown exception");
        }
        return false;
    }
}
