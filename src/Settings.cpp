#include "Settings.h"

#include <Windows.h>
#include <ShlObj.h>

#include <array>
#include <charconv>
#include <format>
#include <stdexcept>

namespace flying_bricks::settings
{
    namespace
    {
        namespace store = RPS::Settings;

        [[nodiscard]] std::filesystem::path settingsDirectory()
        {
            wchar_t documents[MAX_PATH]{};
            if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documents))) {
                throw std::runtime_error("Documents folder is unavailable");
            }
            return std::filesystem::path(documents) / L"My Games" / L"Fallout4VR" / L"Mods_Config" / L"Flying_BRICKS";
        }

        std::vector<store::Definition> definitions()
        {
            using store::Group;
            using store::ValueType;
            return {
                { "General", "DamageModel", "01. General",
                    "The authored per-caliber damage, armor and limb model. false keeps every native damage path.",
                    Group::Player, ValueType::Boolean, "true" },
                { "General", "Force", "01. General",
                    "Projectile force preset: 0 Weapons of Fate, 1 three times the bullet's momentum, 2 vanilla.",
                    Group::Player, ValueType::Integer, "0" },
                { "General", "PlayerHeadshotInstantKill", "01. General",
                    "A head hit that gets through kills the player, as it kills a human NPC. false gives the player\n"
                    "double head damage instead.",
                    Group::Player, ValueType::Boolean, "true" },
                { "Debug", "HitLabels", "02. Debug",
                    "Draws a marker and a label at each recent hit (needs RPS UI Framework).",
                    Group::Player, ValueType::Boolean, "false" },
                { "Debug", "HitTrace", "02. Debug", "Logs each hit's damage calculation to Flying_BRICKS.log.",
                    Group::Player, ValueType::Boolean, "false" },
            };
        }

        // Flying_BRICKS.ini of earlier versions held the same defaults under these keys.
        constexpr auto legacyKey(std::string_view section, std::string_view oldKey, std::string_view key, std::string_view oldDefault)
        {
            store::LegacyKey row{};
            row.oldKey = oldKey;
            row.table = section;
            row.key = key;
            row.oldDefault = oldDefault;
            row.oldSection = section;
            return row;
        }
        // iFlight is not carried over: Real World is the only flight.
        const std::array kLegacyKeys{
            legacyKey("General", "bDamageModel", "DamageModel", "1"),
            legacyKey("General", "iForce", "Force", "0"),
            legacyKey("General", "bPlayerHeadshotInstantKill", "PlayerHeadshotInstantKill", "1"),
            legacyKey("Debug", "bHitLabels", "HitLabels", "0"),
            legacyKey("Debug", "bHitTrace", "HitTrace", "0"),
        };

        bool flag(const store::Store& settings, std::string_view table, std::string_view key, bool fallback)
        {
            const auto* setting = settings.find(table, key);
            return setting ? setting->value == "true" : fallback;
        }

        template <class Enum>
        Enum choice(const store::Store& settings, std::string_view key, std::int32_t count, Enum fallback, std::vector<std::string>& report)
        {
            const auto* setting = settings.find("General", key);
            std::int32_t value{};
            if (setting) {
                const auto& text = setting->value;
                const auto read = std::from_chars(text.data(), text.data() + text.size(), value);
                if (read.ec == std::errc{} && value >= 0 && value < count)
                    return static_cast<Enum>(value);
                report.push_back(std::format("[General] {} = {} is not 0 to {}; using {}", key, text, count - 1,
                    static_cast<std::int32_t>(fallback)));
            }
            return fallback;
        }
    }

    std::unique_ptr<RPS::Settings::Store> makeStore(const std::filesystem::path& directory)
    {
        return std::make_unique<store::Store>(
            store::Files{ directory, "Flying BRICKS", std::string(kFileName), {},
                "## Flying_BRICKS.toml - Flying BRICKS ballistic damage and flight\n"
                "## Read once when the game starts; restart the game after a change.\n"
                "## Your own notes are kept when they start with a single # on the lines above an option.\n",
                {} },
            definitions(), store::LegacyIni{ { directory / "Flying_BRICKS.ini" }, kLegacyKeys });
    }

    Values fromStore(const RPS::Settings::Store& settings, std::vector<std::string>& report)
    {
        const Values defaults;
        Values values;
        values.damageModel = flag(settings, "General", "DamageModel", defaults.damageModel);
        values.force = choice(settings, "Force", kForceCount, defaults.force, report);
        values.playerHeadshotInstantKill = flag(settings, "General", "PlayerHeadshotInstantKill", defaults.playerHeadshotInstantKill);
        values.hitLabels = flag(settings, "Debug", "HitLabels", defaults.hitLabels);
        values.hitTrace = flag(settings, "Debug", "HitTrace", defaults.hitTrace);
        return values;
    }

    Values load(std::vector<std::string>& report)
    {
        const auto directory = settingsDirectory();
        std::filesystem::create_directories(directory);
        auto settings = makeStore(directory);
        settings->load(true);
        report.insert(report.end(), settings->notices().begin(), settings->notices().end());
        report.insert(report.end(), settings->messages().begin(), settings->messages().end());
        return fromStore(*settings, report);
    }
}
