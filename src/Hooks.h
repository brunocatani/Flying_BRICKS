#pragma once

namespace flying_bricks::hooks
{
    // Reads every callsite and confirms it still calls the expected native function. Patches nothing.
    [[nodiscard]] bool preflight() noexcept;

    // Patches every callsite. Call only after preflight() passed and the ammo table is built.
    [[nodiscard]] bool install() noexcept;

    // Off sends every attack down the native path, for A/B comparison on the next hit.
    void setDamageModelActive(bool active) noexcept;

    void setHitTrace(bool enabled) noexcept;
}
