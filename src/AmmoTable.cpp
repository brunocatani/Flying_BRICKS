#include "PCH.h"

#include "AmmoTable.h"

#include "PatchFiles.h"
#include "PluginForms.h"
#include "Terminal.h"

#include <ranges>

namespace flying_bricks::ammo_table
{
    namespace
    {
        struct AmmoRow
        {
            std::string_view plugin;
            std::uint32_t local;
            std::uint32_t normalProjectile;
            float damage;
        };

#include "AmmoData.inl"

        constexpr std::string_view kPluginName = "Flying_BRICKS.esp";

        // The ESP generator gives each profile's suppressed PROJ the normal record's ID plus this.
        constexpr std::uint32_t kSuppressedFormOffset = 0x100;

        // Fallout4.esm forms that mark a conventional bullet.
        constexpr std::uint32_t kStandardProjectileSound = 0x01D565;
        constexpr std::uint32_t kSuppressedMuzzleLight = 0x1F1E46;

        constexpr std::int32_t kSoundLevelSilent = 2;
        constexpr std::int32_t kSoundLevelQuiet = 4;

        std::vector<Entry> s_entries;  // Sorted by ammo pointer; immutable once build() returns.
        const RE::BGSSoundDescriptorForm* s_standardSound = nullptr;
        const RE::TESObjectLIGH* s_suppressedLight = nullptr;

        [[nodiscard]] constexpr char asciiLower(char value) noexcept
        {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
        }

        [[nodiscard]] bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept
        {
            return std::ranges::equal(lhs, rhs, {}, asciiLower, asciiLower);
        }

        [[nodiscard]] bool containsIgnoreCase(std::string_view text, std::string_view token) noexcept
        {
            return !std::ranges::search(text, token, {}, asciiLower, asciiLower).empty();
        }

        [[nodiscard]] bool isOurFile(const RE::TESFile* file) noexcept
        {
            return file && equalsIgnoreCase(file->GetFilename(), kPluginName);
        }

        // The last source file holds the record the game actually loaded.
        [[nodiscard]] bool winningFileIsOurs(const RE::TESForm& form) noexcept
        {
            const auto* files = form.sourceFiles.array;
            return files && !files->empty() && isOurFile(files->back());
        }

        [[nodiscard]] RE::BGSProjectile* suppressedPartner(const RE::BGSProjectile& normal)
        {
            auto* partner = RE::TESForm::GetFormByID<RE::BGSProjectile>(normal.GetFormID() + kSuppressedFormOffset);
            return partner && winningFileIsOurs(*partner) ? partner : nullptr;
        }
    }

    BuildResult build(std::span<const patch_files::RoundPatch> rounds)
    {
        BuildResult result;
        s_standardSound = RE::TESForm::GetFormByID<RE::BGSSoundDescriptorForm>(kStandardProjectileSound);
        s_suppressedLight = RE::TESForm::GetFormByID<RE::TESObjectLIGH>(kSuppressedMuzzleLight);

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            spdlog::error("TESDataHandler is unavailable; no ammunition is owned");
            return result;
        }

        const auto own = [&](RE::TESAmmo* ammo, std::uint32_t normalLocal, float damage, RE::TESAmmo* replacement) {
            auto* normal = plugin_forms::find<RE::BGSProjectile>(*dataHandler, normalLocal, kPluginName);
            auto* suppressed = normal ? suppressedPartner(*normal) : nullptr;
            if (!normal || !suppressed) {
                spdlog::warn(
                    "Ammo {:08X} has no complete projectile pair (normal {:08X}, suppressed {:08X})",
                    ammo->GetFormID(),
                    normal ? normal->GetFormID() : 0u,
                    suppressed ? suppressed->GetFormID() : 0u);
            }
            if (!normal) {
                return;
            }
            const auto* caliber = terminal::findCaliber(*normal);
            if (!caliber) {
                spdlog::warn("Ammo {:08X} has no caliber row and keeps the native armor curve", ammo->GetFormID());
            }
            // Final priority: a later plugin or RobCo Patcher may have changed these.
            ammo->data.projectile = normal;
            ammo->data.damage = damage;
            s_entries.push_back(Entry{ ammo, normal, suppressed, caliber, damage, replacement != nullptr, replacement });
        };

        // A row whose plugin is not loaded resolves to nothing.
        for (const auto& row : kAmmoRows) {
            auto* ammo = plugin_forms::find<RE::TESAmmo>(*dataHandler, row.local, row.plugin);
            if (!ammo) {
                continue;
            }
            if (!winningFileIsOurs(*ammo)) {
                ++result.overriddenByLaterPlugin;
                const auto* files = ammo->sourceFiles.array;
                const auto* winner = files && !files->empty() ? files->back() : nullptr;
                spdlog::warn(
                    "Ammo {:08X}: {} loads after Flying_BRICKS.esp; our numbers still apply, but place Flying_BRICKS.esp last",
                    ammo->GetFormID(),
                    winner ? winner->GetFilename() : "an unknown plugin");
            }
            own(ammo, row.normalProjectile, row.damage, nullptr);
        }
        // Our own records come first, so a patch file cannot turn one into a foreign round. Later rows win, so they go first
        // and the same round named again in another version's file is skipped.
        for (const auto& row : rounds | std::views::reverse) {
            const auto existing = std::ranges::find(s_entries, row.ammo, &Entry::ammo);
            if (existing != s_entries.end()) {
                if (!existing->foreign) {
                    spdlog::warn("Patch files: ammo {:08X} is one of our own rounds and cannot be replaced; row skipped", row.ammo->GetFormID());
                }
                continue;
            }
            own(row.ammo, row.normalProjectile, row.damage, row.replacement);
            ++result.foreign;
        }

        std::ranges::sort(s_entries, {}, &Entry::ammo);
        result.owned = s_entries.size();
        return result;
    }

    const Entry* find(const RE::TESAmmo* ammo) noexcept
    {
        if (!ammo) {
            return nullptr;
        }
        const auto entry = std::ranges::lower_bound(s_entries, ammo, {}, &Entry::ammo);
        return entry != s_entries.end() && entry->ammo == ammo ? std::to_address(entry) : nullptr;
    }

    std::span<const Entry> entries() noexcept
    {
        return s_entries;
    }

    bool isReplaceable(const RE::BGSProjectile& projectile) noexcept
    {
        const auto& data = projectile.data;
        if (data.explosionType || data.light || data.countdownSound || data.deactivateSound ||
            data.defaultWeaponSource || data.decalData || data.explosionProximity > 0.0001F ||
            (data.activeSoundLoop && data.activeSoundLoop != s_standardSound)) {
            return false;
        }

        const std::string_view model = projectile.GetModel() ? projectile.GetModel() : "";
        if (model.empty()) {
            return false;
        }
        const std::string_view muzzle = projectile.muzzleFlashModel.GetModel() ? projectile.muzzleFlashModel.GetModel() : "";
        const std::string_view editorId = projectile.GetFormEditorID() ? projectile.GetFormEditorID() : "";

        static constexpr std::array<std::string_view, 27> kSpecialTokens{
            "grenade", "missile", "rocket", "flame", "plasma", "laser", "mine",
            "incen", "cryo", "freeze", "poison", "acid", "toxic", "shock",
            "electric", "gauss", "railway", "harpoon", "cannon", "flare",
            "syring", "dart", "bolt", "arrow", "nail", "explosive", "burn",
        };
        return std::ranges::none_of(kSpecialTokens, [&](std::string_view token) {
            return containsIgnoreCase(model, token) || containsIgnoreCase(muzzle, token) ||
                   containsIgnoreCase(editorId, token);
        });
    }

    bool isSuppressed(const RE::BGSProjectile& projectile) noexcept
    {
        const auto level = projectile.soundLevel.underlying();
        if (level == kSoundLevelSilent || level == kSoundLevelQuiet ||
            (s_suppressedLight && projectile.data.muzzleFlashLight == s_suppressedLight)) {
            return true;
        }
        const std::string_view model = projectile.GetModel() ? projectile.GetModel() : "";
        const std::string_view muzzle = projectile.muzzleFlashModel.GetModel() ? projectile.muzzleFlashModel.GetModel() : "";
        return containsIgnoreCase(model, "supp") || containsIgnoreCase(muzzle, "supp");
    }
}
