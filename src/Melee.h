#pragma once

#include "Terminal.h"

namespace RE
{
    class TBO_InstanceData;
    class TESObjectWEAP;
}

// What kind of blow a melee weapon lands: how hard, how armor takes it and how it wears a power armor piece.
// Weapons a player can carry have their own number on the same scale as the ammunition, a 100-health unarmored
// human. Creatures, robots and other mods' weapons keep the number the game computes for them and only take the
// armor rule, since a level-scaled deathclaw or a mod's own weapon has no entry to read.
//
// Thread model: build() runs once on the F4SE message thread before any hook is patched, and the tables never
// change afterwards. The queries run on any hit thread and only read.
namespace flying_bricks::melee
{
    struct Blow
    {
        float damage{};                       // Zero keeps the number the game computed.
        const terminal::Protection* protection{};
        float wearPerDamage{};                // Points off a suit piece per point of damage.
    };

    void build();

    // bash: a gun or other non-melee weapon used as a club.
    [[nodiscard]] const Blow& classify(const RE::TESObjectWEAP& weapon, const RE::TBO_InstanceData* instance, bool bash) noexcept;

    // The blow's number with the weapon's damage mods on top, or zero to keep the game's.
    [[nodiscard]] float damageOf(const Blow& blow, const RE::TESObjectWEAP& weapon, const RE::TBO_InstanceData* instance) noexcept;

    // Points off the suit piece it struck. physicalDamage is the hit's field after armor reduced it.
    [[nodiscard]] float pieceWear(const Blow& blow, const RE::TESObjectWEAP& weapon, const RE::TBO_InstanceData* instance, float physicalDamage) noexcept;
}
