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

        // A form (ammunition or projectile) and the family of the shots that carry it, sorted by form.
        struct FamilyOfForm
        {
            const RE::TESForm* form;
            Thermal family;
        };

        std::vector<FamilyOfForm> s_ammo;
        std::vector<FamilyOfForm> s_projectiles;
        const RE::BGSDamageType* s_energyDamageType = nullptr;
        const RE::BGSKeyword* s_incendiaryThrowKeyword = nullptr;
        const RE::ActorValueInfo* s_energyResistance = nullptr;

        [[nodiscard]] float scaled(float referenceValue, float referenceDamage, float nativeValue) noexcept
        {
            return nativeValue > 0.0F ? referenceDamage * std::pow(nativeValue / referenceValue, kEnergyExponent) : 0.0F;
        }

        // Sorts by form; of several rows for one form the last added wins, so a patch file overrides the base table.
        void sortKeepingLast(std::vector<FamilyOfForm>& rows)
        {
            std::ranges::stable_sort(rows, {}, &FamilyOfForm::form);
            std::vector<FamilyOfForm> merged;
            merged.reserve(rows.size());
            for (const auto& row : rows) {
                if (!merged.empty() && merged.back().form == row.form) {
                    merged.back() = row;
                } else {
                    merged.push_back(row);
                }
            }
            rows = std::move(merged);
        }

        [[nodiscard]] std::optional<Thermal> find(const std::vector<FamilyOfForm>& rows, const RE::TESForm* form) noexcept
        {
            const auto row = std::ranges::lower_bound(rows, form, {}, &FamilyOfForm::form);
            return row != rows.end() && row->form == form ? std::optional{ row->family } : std::nullopt;
        }
    }

    static_assert(static_cast<int>(patch_format::EnergyFamily::Laser) == static_cast<int>(Thermal::Laser));
    static_assert(static_cast<int>(patch_format::EnergyFamily::Plasma) == static_cast<int>(Thermal::Plasma));
    static_assert(static_cast<int>(patch_format::EnergyFamily::Fire) == static_cast<int>(Thermal::Fire));

    BuildResult build(std::span<const patch_files::EnergyPatch> patched)
    {
        BuildResult result;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            spdlog::error("TESDataHandler is unavailable; energy weapons keep the game's damage");
            return result;
        }
        for (const auto& row : kAmmoFamilyRows) {
            if (const auto* ammo = plugin_forms::find<RE::TESAmmo>(*dataHandler, row.local, row.plugin)) {
                s_ammo.push_back(FamilyOfForm{ ammo, row.family });
            } else {
                spdlog::error("Energy ammunition {:06X} from {} was not found", row.local, row.plugin);
            }
        }
        for (const auto& row : kFlameProjectileRows) {
            if (const auto* projectile = plugin_forms::find<RE::BGSProjectile>(*dataHandler, row.local, row.plugin)) {
                s_projectiles.push_back(FamilyOfForm{ projectile, Thermal::Fire });
                ++result.flameProjectiles;
            } else {
                spdlog::error("Flame projectile {:06X} from {} was not found", row.local, row.plugin);
            }
        }
        for (const auto& row : patched) {
            auto& rows = row.projectile ? s_projectiles : s_ammo;
            rows.push_back(FamilyOfForm{ row.projectile ? static_cast<const RE::TESForm*>(row.projectile) : row.ammo, static_cast<Thermal>(row.family) });
            ++(row.projectile ? result.patchedProjectiles : result.patchedAmmunition);
        }
        sortKeepingLast(s_ammo);
        sortKeepingLast(s_projectiles);
        s_incendiaryThrowKeyword = plugin_forms::find<RE::BGSKeyword>(*dataHandler, kIncendiaryThrowKeyword.local, kIncendiaryThrowKeyword.plugin);
        if (!s_incendiaryThrowKeyword) {
            spdlog::error("Incendiary throw keyword {:06X} was not found; Molotovs keep the game's damage", kIncendiaryThrowKeyword.local);
        }
        // Fallout4.esm's load index is always zero, so the local ID is the runtime ID.
        s_energyDamageType = RE::TESForm::GetFormByID<RE::BGSDamageType>(kEnergyDamageType);
        s_energyResistance = s_energyDamageType ? s_energyDamageType->data.resistance : nullptr;
        if (!s_energyResistance) {
            spdlog::error("The energy damage type has no resistance; energy weapons keep the game's damage");
            s_ammo.clear();
            s_projectiles.clear();
        }
        result.ammunition = s_ammo.size();
        result.projectiles = s_projectiles.size();
        result.incendiaryThrow = s_incendiaryThrowKeyword != nullptr;
        return result;
    }

    std::optional<Thermal> familyOf(const RE::BGSProjectile* overrideProjectile, const RE::TESAmmo* ammo) noexcept
    {
        if (overrideProjectile) {
            if (const auto family = find(s_projectiles, overrideProjectile)) {
                return family;
            }
        }
        return ammo ? find(s_ammo, ammo) : std::nullopt;
    }

    bool carriesEnergyDamage(const RE::TESObjectWEAP::InstanceData& instance) noexcept
    {
        if (!s_energyDamageType || !instance.damageTypes) {
            return false;
        }
        return std::ranges::any_of(*instance.damageTypes, [](const auto& entry) { return entry.first == s_energyDamageType && entry.second.i > 0; });
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
