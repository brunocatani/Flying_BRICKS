#include "PCH.h"

#include "Presets.h"

#include "AmmoTable.h"
#include "Terminal.h"

namespace flying_bricks::presets
{
    namespace
    {
        struct Flight
        {
            float speed;
            float gravity;
            float range;
            float collisionRadius;
            float lifetime;
        };

        struct TrajectoryClass
        {
            // The values the ESP authors, which only identify the class at load.
            float authoredSpeed;
            float authoredForce;
            Flight flight;
            std::array<float, settings::kForceCount> forces;
        };

        // Speeds are game units per second at 70 units per metre. Each caliber flies at its own muzzle
        // velocity with its own air drag from data/CartridgeFlight.json (the class speed below only
        // serves a projectile with no caliber), under true gravity, with range and lifetime long enough
        // not to cut a shot short. Drag changes speed and drop only: damage comes from the ammunition
        // record, whatever the round's speed.
        // Forces: Weapons of Fate's; Momentum is 3 x bullet momentum in kg m/s, per pellet for
        // shotguns, which puts .308 at the Weapons of Fate value; Vanilla is the base game's.
        constexpr float kLongRange = 200000.0F;
        constexpr std::array kClasses{
            TrajectoryClass{ 27046.0F, 16.0F, Flight{ 26600.0F, 1.0F, kLongRange, 0.4F, 1.5F }, { 16.0F, 14.0F, 8.0F } },  // 10mm, 380 m/s
            TrajectoryClass{ 65189.0F, 25.0F, Flight{ 59500.0F, 1.0F, kLongRange, 0.4F, 3.0F }, { 25.0F, 25.0F, 7.0F } },  // .308, 850 m/s
            TrajectoryClass{ 20805.0F, 12.0F, Flight{ 20300.0F, 1.0F, kLongRange, 0.4F, 1.5F }, { 12.0F, 6.0F, 8.0F } },  // .38 Special, 290 m/s
            TrajectoryClass{ 24966.0F, 20.0F, Flight{ 28000.0F, 1.0F, kLongRange, 0.4F, 1.5F }, { 20.0F, 19.0F, 8.0F } },  // .44 Magnum, 400 m/s
            TrajectoryClass{ 25867.0F, 23.0F, Flight{ 18200.0F, 1.0F, kLongRange, 0.4F, 1.5F }, { 23.0F, 11.0F, 8.0F } },  // .45 ACP, 260 m/s
            TrajectoryClass{ 33773.44921875F, 25.0F, Flight{ 28000.0F, 1.0F, kLongRange, 0.4F, 2.0F }, { 25.0F, 31.0F, 8.0F } },  // .45-70, 400 m/s
            TrajectoryClass{ 64356.0F, 35.0F, Flight{ 62300.0F, 1.0F, kLongRange, 0.4F, 4.0F }, { 35.0F, 112.0F, 7.0F } },  // .50 BMG, 890 m/s
            TrajectoryClass{ 57768.0F, 10.0F, Flight{ 65100.0F, 1.0F, kLongRange, 0.4F, 3.0F }, { 10.0F, 10.0F, 3.0F } },  // 4.73 caseless, 930 m/s
            TrajectoryClass{ 50646.30078125F, 20.0F, Flight{ 50050.0F, 1.0F, kLongRange, 0.4F, 3.0F }, { 20.0F, 17.0F, 7.0F } },  // 7.62x39, 715 m/s
            TrajectoryClass{ 65189.0F, 16.0F, Flight{ 65800.0F, 1.0F, kLongRange, 0.4F, 3.0F }, { 16.0F, 11.0F, 7.0F } },  // 5.56, 940 m/s
            TrajectoryClass{ 16900.0F, 10.0F, Flight{ 28000.0F, 1.0F, kLongRange, 0.25F, 0.5F }, { 10.0F, 5.0F, 4.0F } },  // 12 gauge buckshot, 400 m/s
        };

        struct OwnedProjectile
        {
            RE::BGSProjectile* form;  // Non-owning; forms live for the whole session.
            const TrajectoryClass* trajectoryClass;
            const terminal::Caliber* caliber;  // Null keeps the class speed and no drag.
        };

        struct DragEntry
        {
            const void* form;
            float factor;
        };

        std::vector<OwnedProjectile> s_projectiles;
        std::vector<DragEntry> s_drag;  // Sorted by form; read-only after classify().

        // The standard G1 drag function by Mach number. Sound is taken at sea level, 15 degrees.
        struct DragPoint
        {
            float mach;
            float drag;
        };
        constexpr std::array kG1{
            DragPoint{ 0.0F, 0.2629F }, DragPoint{ 0.5F, 0.2032F }, DragPoint{ 0.7F, 0.2165F }, DragPoint{ 0.8F, 0.2546F }, DragPoint{ 0.9F, 0.3415F },
            DragPoint{ 0.95F, 0.4084F }, DragPoint{ 1.0F, 0.4805F }, DragPoint{ 1.05F, 0.5427F }, DragPoint{ 1.1F, 0.5883F }, DragPoint{ 1.2F, 0.6393F },
            DragPoint{ 1.35F, 0.6621F }, DragPoint{ 1.5F, 0.6573F }, DragPoint{ 1.75F, 0.6280F }, DragPoint{ 2.0F, 0.5934F }, DragPoint{ 2.5F, 0.5397F },
            DragPoint{ 3.0F, 0.5133F }, DragPoint{ 3.5F, 0.5040F }, DragPoint{ 4.0F, 0.5006F }
        };
        constexpr float kExplosionTimerLead = 0.05F;
        constexpr float kSpeedOfSound = 340.0F;
        constexpr float kUnitsPerMetre = 70.0F;

        [[nodiscard]] float g1Drag(float mach) noexcept
        {
            for (std::size_t index = 1; index < kG1.size(); ++index) {
                if (mach <= kG1[index].mach) {
                    const auto& low = kG1[index - 1];
                    const auto& high = kG1[index];
                    return low.drag + (high.drag - low.drag) * (mach - low.mach) / (high.mach - low.mach);
                }
            }
            return kG1.back().drag;
        }

        [[nodiscard]] const TrajectoryClass* findClass(const RE::BGSProjectile& projectile) noexcept
        {
            for (const auto& candidate : kClasses) {
                if (projectile.data.speed == candidate.authoredSpeed && projectile.data.force == candidate.authoredForce) {
                    return &candidate;
                }
            }
            return nullptr;
        }
    }

    ClassifyResult classify()
    {
        ClassifyResult result;
        const auto add = [&](RE::BGSProjectile* form, const terminal::Caliber* caliber) {
            if (!form) {
                return;
            }
            if (const auto* trajectoryClass = findClass(*form)) {
                s_projectiles.push_back(OwnedProjectile{ form, trajectoryClass, caliber });
                if (caliber) {
                    s_drag.push_back(DragEntry{ form, caliber->dragFactor });
                }
                ++result.classified;
            } else {
                ++result.unknown;
                spdlog::warn("Projectile {:08X} matches no trajectory class and keeps its record values", form->GetFormID());
            }
        };
        for (const auto& entry : ammo_table::entries()) {
            if (entry.foreign) {
                continue;  // Shares the projectiles of our item for the caliber.
            }
            add(entry.normal, entry.caliber);
            add(entry.suppressed, entry.caliber);
        }
        std::ranges::sort(s_drag, {}, &DragEntry::form);
        return result;
    }

    void apply(settings::Force force) noexcept
    {
        const auto forceIndex = static_cast<std::size_t>(force);
        for (const auto& [form, trajectoryClass, caliber] : s_projectiles) {
            const auto& values = trajectoryClass->flight;
            auto& data = form->data;
            data.speed = caliber ? caliber->realWorldSpeed : values.speed;
            data.gravity = values.gravity;
            data.range = values.range;
            data.collisionRadius = values.collisionRadius;
            data.lifetime = values.lifetime;
            // A BIBFX impact also fires on this timer, so it must only mark the end of flight, as BIBFX authors it.
            data.explosionTimer = std::max(values.lifetime - kExplosionTimerLead, 0.0F);
            data.force = trajectoryClass->forces[forceIndex];
        }
    }

    float dragFactorOf(const void* projectileRecord) noexcept
    {
        const auto row = std::ranges::lower_bound(s_drag, projectileRecord, {}, &DragEntry::form);
        return row != s_drag.end() && row->form == projectileRecord ? row->factor : 0.0F;
    }

    float speedAfterDrag(float speed, float factor, float delta) noexcept
    {
        const float metres = speed / kUnitsPerMetre;
        const float slowed = metres - factor * g1Drag(metres / kSpeedOfSound) * metres * metres * delta;
        return std::max(slowed, 0.0F) * kUnitsPerMetre;
    }

    terminal::Caliber atDistance(const terminal::Caliber& caliber, float distance) noexcept
    {
        constexpr float kStepMetres = 5.0F;
        constexpr float kLongestShotMetres = 5000.0F;  // Past every round's lifetime.
        auto impact = caliber;
        if (!(distance > 0.0F) || caliber.realWorldSpeed <= 0.0F || caliber.dragFactor <= 0.0F) {
            return impact;
        }
        const float muzzle = caliber.realWorldSpeed / kUnitsPerMetre;
        float speed = muzzle;
        // Over distance the drag is dv/dx = -factor x G1 x v; each step holds G1 at the step's start.
        for (float remaining = std::min(distance / kUnitsPerMetre, kLongestShotMetres); remaining > 0.0F; remaining -= kStepMetres) {
            speed *= std::exp(-caliber.dragFactor * g1Drag(speed / kSpeedOfSound) * std::min(remaining, kStepMetres));
        }
        const float kept = speed / muzzle;
        impact.energyKept = kept * kept;
        return impact;
    }
}
