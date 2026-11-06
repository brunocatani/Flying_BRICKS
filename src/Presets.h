#pragma once

#include "Settings.h"
#include "Terminal.h"

#include <cstddef>

// How our projectiles fly and push. The ESP carries one PROJ pair per caliber with the Weapons of Fate
// values, which identify each record's trajectory class at load. apply() rewrites those records in
// memory with the real-world flight (each caliber's muzzle velocity, G1 air drag, true gravity) and the
// chosen force preset. The change is not saved in the game's save file, and only forms owned by
// Flying_BRICKS.esp are touched.
//
// Thread model: apply() runs on the game's main thread. Shots read these fields on the firing
// thread; each is a single aligned float, so a shot sees the old or the new value of each field.
namespace flying_bricks::presets
{
    struct ClassifyResult
    {
        std::size_t classified{};
        // A later plugin changed the record, so it matches no class and is left alone.
        std::size_t unknown{};
    };

    // Reads each owned projectile's authored values to find its trajectory class. Call once, after
    // the ammo table is built and before the first apply().
    [[nodiscard]] ClassifyResult classify();

    void apply(settings::Force force) noexcept;

    // The speed a flying round has after one step of air drag, in game units per second. Zero factor
    // leaves it unchanged. The drag factor for a projectile record, or zero for a record we do not own.
    // Both are read on projectile update threads; the table never changes after classify().
    [[nodiscard]] float dragFactorOf(const void* projectileRecord) noexcept;
    [[nodiscard]] float speedAfterDrag(float speed, float factor, float delta) noexcept;

    // The round as it arrives after flying this far, in game units, under the same drag: energyKept is the
    // square of the speed it kept. Zero or an unknown distance returns the round at the muzzle. Bounded work:
    // at most 1000 steps.
    [[nodiscard]] terminal::Caliber atDistance(const terminal::Caliber& caliber, float distance) noexcept;
}
