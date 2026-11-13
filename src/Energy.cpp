#include "PCH.h"

#include "Energy.h"

#include "PluginForms.h"

#include <cmath>

namespace flying_bricks::energy
{
    namespace
    {
        using terminal::Thermal;

        struct FamilyRow
        {
            float referenceValue;   // A native typed value...
            float referenceDamage;  // ...and the damage it does on the caliber scale.
            bool rangeFalloff;
        };

        struct AmmoFamilyRow
        {
            std::string_view plugin;
            std::uint32_t local;
            Thermal family;
        };

        struct FormRow
        {
            std::string_view plugin;
            std::uint32_t local;
        };

#include "EnergyData.inl"

        constexpr float kEnergyExponent = 0.6F;  // The damage model's own exponent.

        struct ResolvedAmmo
        {
            const RE::TESAmmo* form;
            Thermal family;
        };

        std::vector<ResolvedAmmo> s_ammo;                    // Sorted by form.
        std::vector<const RE::BGSProjectile*> s_flameProjectiles;  // Sorted.
        const RE::BGSKeyword* s_incendiaryThrowKeyword = nullptr;
        const RE::ActorValueInfo* s_energyResistance = nullptr;

        [[nodiscard]] float scaled(float referenceValue, float referenceDamage, float nativeValue) noexcept
        {
            return nativeValue > 0.0F ? referenceDamage * std::pow(nativeValue / referenceValue, kEnergyExponent) : 0.0F;
        }
    }

    BuildResult build()
    {
        BuildResult result;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            spdlog::error("TESDataHandler is unavailable; energy weapons keep the game's damage");
            return result;
        }
        for (const auto& row : kAmmoFamilyRows) {
            if (const auto* ammo = plugin_forms::find<RE::TESAmmo>(*dataHandler, row.local, row.plugin)) {
                s_ammo.push_back(ResolvedAmmo{ ammo, row.family });
            } else {
                spdlog::error("Energy ammunition {:06X} from {} was not found", row.local, row.plugin);
            }
        }
        std::ranges::sort(s_ammo, {}, &ResolvedAmmo::form);
        for (const auto& row : kFlameProjectileRows) {
            if (const auto* projectile = plugin_forms::find<RE::BGSProjectile>(*dataHandler, row.local, row.plugin)) {
                s_flameProjectiles.push_back(projectile);
            } else {
                spdlog::error("Flame projectile {:06X} from {} was not found", row.local, row.plugin);
            }
        }
        std::ranges::sort(s_flameProjectiles);
        s_incendiaryThrowKeyword = plugin_forms::find<RE::BGSKeyword>(*dataHandler, kIncendiaryThrowKeyword.local, kIncendiaryThrowKeyword.plugin);
        if (!s_incendiaryThrowKeyword) {
            spdlog::error("Incendiary throw keyword {:06X} was not found; Molotovs keep the game's damage", kIncendiaryThrowKeyword.local);
        }
        // Fallout4.esm's load index is always zero, so the local ID is the runtime ID.
        const auto* damageType = RE::TESForm::GetFormByID<RE::BGSDamageType>(kEnergyDamageType);
        s_energyResistance = damageType ? damageType->data.resistance : nullptr;
        if (!s_energyResistance) {
            spdlog::error("The energy damage type has no resistance; energy weapons keep the game's damage");
            s_ammo.clear();
            s_flameProjectiles.clear();
        }
        result.ammunition = s_ammo.size();
        result.flameProjectiles = s_flameProjectiles.size();
        result.incendiaryThrow = s_incendiaryThrowKeyword != nullptr;
        return result;
    }

    std::optional<Thermal> familyOf(const RE::BGSProjectile* overrideProjectile, const RE::TESAmmo* ammo) noexcept
    {
        if (overrideProjectile && std::ranges::binary_search(s_flameProjectiles, overrideProjectile)) {
            return Thermal::Fire;
        }
        if (!ammo) {
            return std::nullopt;
        }
        const auto row = std::ranges::lower_bound(s_ammo, ammo, {}, &ResolvedAmmo::form);
        if (row == s_ammo.end() || row->form != ammo) {
            return std::nullopt;
        }
        return row->family;
    }

    bool isIncendiaryThrow(const RE::TESObjectWEAP& weapon) noexcept
    {
        return s_incendiaryThrowKeyword && s_energyResistance && weapon.HasKeyword(s_incendiaryThrowKeyword);
    }

    float authoredDamage(Thermal family, float nativeValue) noexcept
    {
        const auto& row = kFamilyRows[static_cast<std::size_t>(family)];
        return scaled(row.referenceValue, row.referenceDamage, nativeValue);
    }

    float incendiaryThrowDamage(float nativeDamage) noexcept
    {
        return scaled(kIncendiaryThrowReferenceValue, kIncendiaryThrowReferenceDamage, nativeDamage);
    }

    bool keepsRangeFalloff(Thermal family) noexcept
    {
        return kFamilyRows[static_cast<std::size_t>(family)].rangeFalloff;
    }

    const RE::ActorValueInfo* energyResistance() noexcept
    {
        return s_energyResistance;
    }
}
