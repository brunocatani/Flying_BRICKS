#include "PCH.h"

#include "PatchFiles.h"

#include "PluginForms.h"

#include <fstream>
#include <sstream>

namespace flying_bricks::patch_files
{
    namespace
    {
        using namespace std::string_view_literals;
        using patch_format::CaliberRow;

#include "CaliberData.inl"

        constexpr std::string_view kOurPlugin = "Flying_BRICKS.esp";

        [[nodiscard]] std::filesystem::path patchFolder()
        {
            return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Flying_BRICKS" / "Patches";
        }

        [[nodiscard]] bool endsWithIgnoreCase(std::string_view text, std::string_view suffix) noexcept
        {
            return text.size() >= suffix.size() && _strnicmp(text.data() + text.size() - suffix.size(), suffix.data(), suffix.size()) == 0;
        }

        [[nodiscard]] bool namesPlugin(std::string_view name) noexcept
        {
            return endsWithIgnoreCase(name, ".esp") || endsWithIgnoreCase(name, ".esm") || endsWithIgnoreCase(name, ".esl");
        }

        // The name a plugin is loaded under, trying its other extensions: a mod can ship the same plugin as .esp and .esl, and
        // a patch or a row names only one of them.
        [[nodiscard]] std::string loadedVersion(RE::TESDataHandler& dataHandler, std::string_view plugin)
        {
            if (plugin_forms::findLoaded(dataHandler, plugin)) {
                return std::string{ plugin };
            }
            const auto dot = plugin.rfind('.');
            if (dot == std::string_view::npos) {
                return {};
            }
            for (const auto extension : { ".esp"sv, ".esl"sv, ".esm"sv }) {
                auto name = std::string{ plugin.substr(0, dot) }.append(extension);
                if (plugin_forms::findLoaded(dataHandler, name)) {
                    return name;
                }
            }
            return {};
        }

        [[nodiscard]] const CaliberRow* findCaliber(std::string_view id) noexcept
        {
            const auto row = std::ranges::find(kCaliberRows, id, &CaliberRow::id);
            return row != kCaliberRows.end() ? std::to_address(row) : nullptr;
        }

        [[nodiscard]] std::string readFile(const std::filesystem::path& path)
        {
            std::ifstream stream{ path, std::ios::binary };
            std::ostringstream text;
            text << stream.rdbuf();
            return std::move(text).str();
        }

        struct FileCounts
        {
            std::size_t applied{};
            std::size_t missing{};
        };

        // The round a weapon or part row names. Null when the row is bad or its round is not loaded; both are logged.
        [[nodiscard]] RE::TESAmmo* resolveRound(RE::TESDataHandler& dataHandler, const patch_format::TargetRow& row, std::string_view plugin, RE::TESLevItem** list, Patches& patches)
        {
            if (row.caliber.empty()) {
                auto* ammo = plugin_forms::find<RE::TESAmmo>(dataHandler, row.ammoLocal, loadedVersion(dataHandler, row.ammoPlugin));
                if (!ammo) {
                    spdlog::warn("Patch file {}: {:06X} names {}|{:06X}, which is not loaded; row skipped", plugin, row.local, row.ammoPlugin, row.ammoLocal);
                    ++patches.problems;
                }
                return ammo;
            }
            const auto* caliber = findCaliber(row.caliber);
            auto* ammo = caliber ? plugin_forms::find<RE::TESAmmo>(dataHandler, caliber->local, caliber->plugin) : nullptr;
            if (!ammo) {
                spdlog::warn("Patch file {}: {:06X} names caliber {}, which does not exist; row skipped", plugin, row.local, row.caliber);
                ++patches.problems;
                return nullptr;
            }
            if (list) {
                *list = plugin_forms::find<RE::TESLevItem>(dataHandler, caliber->ammoList, kOurPlugin);
            }
            return ammo;
        }

        void applyFile(RE::TESDataHandler& dataHandler, std::string_view plugin, const patch_format::File& file, Patches& patches)
        {
            FileCounts counts;
            const auto notFound = [&](std::string_view kind, std::uint32_t local) {
                spdlog::info("Patch file {}: {} {:06X} is not in the plugin; row skipped", plugin, kind, local);
                ++counts.missing;
            };

            for (const auto& row : file.rounds) {
                auto* ammo = plugin_forms::find<RE::TESAmmo>(dataHandler, row.local, plugin);
                if (!ammo) {
                    notFound("round", row.local);
                    continue;
                }
                const auto* caliber = findCaliber(row.caliber);
                auto* replacement = caliber ? plugin_forms::find<RE::TESAmmo>(dataHandler, caliber->local, caliber->plugin) : nullptr;
                if (!replacement) {
                    spdlog::warn("Patch file {}: round {:06X} names caliber {}, which does not exist; row skipped", plugin, row.local, row.caliber);
                    ++patches.problems;
                    continue;
                }
                patches.rounds.push_back(RoundPatch{ ammo, replacement, caliber->normalProjectile, caliber->damage });
                ++counts.applied;
            }
            for (const auto& row : file.weapons) {
                auto* weapon = plugin_forms::find<RE::TESObjectWEAP>(dataHandler, row.local, plugin);
                if (!weapon) {
                    notFound("weapon", row.local);
                    continue;
                }
                RE::TESLevItem* list = nullptr;
                if (auto* ammo = resolveRound(dataHandler, row, plugin, &list, patches)) {
                    patches.weapons.push_back(WeaponPatch{ weapon, ammo, list });
                    ++counts.applied;
                }
            }
            for (const auto& row : file.parts) {
                auto* part = plugin_forms::find<RE::BGSMod::Attachment::Mod>(dataHandler, row.local, plugin);
                if (!part) {
                    notFound("part", row.local);
                    continue;
                }
                if (auto* ammo = resolveRound(dataHandler, row, plugin, nullptr, patches)) {
                    patches.parts.push_back(PartPatch{ part, ammo });
                    ++counts.applied;
                }
            }
            for (const auto& row : file.damage) {
                auto* weapon = plugin_forms::find<RE::TESObjectWEAP>(dataHandler, row.local, plugin);
                if (!weapon) {
                    notFound("damage", row.local);
                    continue;
                }
                patches.damage.push_back(DamagePatch{ weapon, row.damage });
                ++counts.applied;
            }
            for (const auto& row : file.defaults) {
                auto* weapon = plugin_forms::find<RE::TESObjectWEAP>(dataHandler, row.local, plugin);
                if (!weapon) {
                    notFound("default", row.local);
                    continue;
                }
                auto* from = plugin_forms::find<RE::BGSMod::Attachment::Mod>(dataHandler, row.fromLocal, loadedVersion(dataHandler, row.fromPlugin));
                auto* to = plugin_forms::find<RE::BGSMod::Attachment::Mod>(dataHandler, row.toLocal, loadedVersion(dataHandler, row.toPlugin));
                if (!from || !to) {
                    spdlog::warn("Patch file {}: default {:06X} names a part that is not loaded; row skipped", plugin, row.local);
                    ++patches.problems;
                    continue;
                }
                // A part only fits the slot it was made for, so a part for another slot would leave the gun without one.
                if (from->attachPoint.keywordIndex != to->attachPoint.keywordIndex) {
                    spdlog::warn("Patch file {}: default {:06X} swaps parts for different attach points; row skipped", plugin, row.local);
                    ++patches.problems;
                    continue;
                }
                patches.defaults.push_back(DefaultPatch{ weapon, from, to });
                ++counts.applied;
            }
            patches.formsNotFound += counts.missing;
            spdlog::info("Patch file {}: {} rows, {} not in the plugin", plugin, counts.applied, counts.missing);
        }
    }

    Patches load()
    {
        Patches patches;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            spdlog::error("TESDataHandler is unavailable; no patch file is read");
            return patches;
        }
        std::error_code error;
        std::filesystem::directory_iterator files{ patchFolder(), error };
        if (error) {
            spdlog::warn("Patch files: the folder was not found, so no weapon is patched ({})", error.message());
            return patches;
        }
        // Every file for a plugin's base name applies to whichever version is loaded, because a version can be missing rows
        // the other has. A row whose ID that version lacks is skipped, so a compacted plugin is covered by its own rows. The
        // file named exactly after the loaded plugin goes last, so its rows win.
        struct Pending
        {
            std::filesystem::path path;
            std::string plugin;
        };
        std::vector<Pending> exact;
        std::vector<Pending> otherVersion;
        for (const auto& entry : files) {
            // One bad file must never cost the others, and a file name may not convert to the game's code page.
            try {
                const auto& path = entry.path();
                if (_wcsicmp(path.extension().c_str(), L".toml") != 0) {
                    continue;
                }
                const auto stem = path.stem().string();
                if (!namesPlugin(stem)) {
                    spdlog::warn("Patch file {}.toml: the name must be the plugin's, such as MyGun.esp.toml; file skipped", stem);
                    ++patches.problems;
                    continue;
                }
                auto plugin = loadedVersion(*dataHandler, stem);
                if (plugin.empty()) {
                    ++patches.filesNotLoaded;
                    continue;
                }
                const bool isExact = _stricmp(plugin.c_str(), stem.c_str()) == 0;
                (isExact ? exact : otherVersion).push_back(Pending{ path, std::move(plugin) });
            } catch (const std::exception& failure) {
                spdlog::warn("Patch files: one file was skipped: {}", failure.what());
                ++patches.problems;
            }
        }
        otherVersion.insert_range(otherVersion.end(), exact);
        for (const auto& pending : otherVersion) {
            try {
                const auto file = patch_format::parse(readFile(pending.path));
                for (const auto& problem : file.problems) {
                    spdlog::warn("Patch file {}: {}", pending.plugin, problem);
                }
                patches.problems += file.problems.size();
                applyFile(*dataHandler, pending.plugin, file, patches);
                ++patches.filesRead;
            } catch (const std::exception& failure) {
                spdlog::warn("Patch files: one file was skipped: {}", failure.what());
                ++patches.problems;
            }
        }
        return patches;
    }

    std::span<const patch_format::CaliberRow> calibers() noexcept
    {
        return kCaliberRows;
    }
}
