#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#define TOML_ENABLE_WINDOWS_COMPAT 0
#define TOML_ENABLE_FORMATTERS 0
#include <toml++/toml.hpp>

// The format of a patch file: one TOML file per plugin, named after it, that anyone can write without building this mod.
// This is a public contract, and it holds no engine types so that a test can read the files the mod ships.
//
//   [[weapon]]  id = "000F9B"  caliber = "9x19"                 the gun fires this caliber
//   [[part]]    id = "0012AB"  ammo = "Fallout4.esm|01F276"     the object mod fires this exact record
//   [[round]]   id = "000FA8"  caliber = "9x19"                 this custom round becomes ours everywhere
//   [[damage]]  id = "000805"  damage = 36                      the gun's base damage, if another mod set it to 0
//   [[default]] id = "000F99"  from = "A.esp|003607"  to = "A.esp|003606"
//                                                               this gun spawns with part to instead of part from
//
// An id is the form's hex ID. A load order prefix is dropped, so an ID copied from the game or an editor works. name is an
// optional label on any row. A bad row is reported and skipped, and the other rows of the file still apply.
namespace flying_bricks::patch_format
{
    // One of our calibers: the item that stands for it, and what a custom round replaced by it needs.
    struct CaliberRow
    {
        std::string_view id;
        std::string_view plugin;
        std::uint32_t local;
        std::uint32_t normalProjectile;
        float damage;
        std::uint32_t ammoList;
        bool worldList;  // Base game rounds stay out: the game's own lists already hold them.
    };

    struct TargetRow
    {
        std::uint32_t local{};
        std::string caliber;     // Empty when the row names an exact record.
        std::string ammoPlugin;  // Empty when the row names a caliber.
        std::uint32_t ammoLocal{};
    };

    struct RoundRow
    {
        std::uint32_t local{};
        std::string caliber;
    };

    struct DamageRow
    {
        std::uint32_t local{};
        std::uint16_t damage{};
    };

    struct DefaultRow
    {
        std::uint32_t local{};
        std::string fromPlugin;
        std::uint32_t fromLocal{};
        std::string toPlugin;
        std::uint32_t toLocal{};
    };

    struct File
    {
        std::vector<TargetRow> weapons;
        std::vector<TargetRow> parts;
        std::vector<RoundRow> rounds;
        std::vector<DamageRow> damage;
        std::vector<DefaultRow> defaults;
        std::vector<std::string> problems;
    };

    [[nodiscard]] inline bool parseLocal(std::string_view text, std::uint32_t& local) noexcept
    {
        if (text.starts_with("0x") || text.starts_with("0X")) {
            text.remove_prefix(2);
        }
        std::uint32_t value = 0;
        if (text.empty() || text.size() > 8) {
            return false;
        }
        const auto end = text.data() + text.size();
        const auto [position, error] = std::from_chars(text.data(), end, value, 16);
        if (error != std::errc{} || position != end) {
            return false;
        }
        // FE marks a light plugin: its next three digits are the plugin's slot and only the last three are the record's.
        local = (value >> 24) == 0xFE ? value & 0xFFF : value & 0xFFFFFF;
        return true;
    }

    // Reads "Plugin.esp|01F276".
    [[nodiscard]] inline bool parseForm(std::string_view text, std::string& plugin, std::uint32_t& local)
    {
        const auto separator = text.rfind('|');
        if (separator == std::string_view::npos || separator == 0 || !parseLocal(text.substr(separator + 1), local)) {
            return false;
        }
        plugin = text.substr(0, separator);
        return true;
    }

    namespace detail
    {
        [[nodiscard]] inline bool readId(const toml::table& row, std::string_view section, std::size_t index, File& file, std::uint32_t& local)
        {
            const auto text = row["id"].value<std::string_view>();
            if (text && parseLocal(*text, local)) {
                return true;
            }
            file.problems.push_back(std::string{ section } + " row " + std::to_string(index + 1) + ": id must be a hex form ID in quotes");
            return false;
        }

        [[nodiscard]] inline std::string describe(std::string_view section, std::size_t index, std::string_view message)
        {
            return std::string{ section } + " row " + std::to_string(index + 1) + ": " + std::string{ message };
        }

        inline void readTargets(const toml::table& root, std::string_view section, std::vector<TargetRow>& rows, File& file)
        {
            const auto* array = root[section].as_array();
            if (!array) {
                return;
            }
            for (std::size_t index = 0; index < array->size(); ++index) {
                const auto* row = array->get(index)->as_table();
                TargetRow target;
                if (!row) {
                    file.problems.push_back(describe(section, index, "not a table"));
                    continue;
                }
                if (!readId(*row, section, index, file, target.local)) {
                    continue;
                }
                const auto caliber = (*row)["caliber"].value<std::string_view>();
                const auto ammo = (*row)["ammo"].value<std::string_view>();
                if (caliber.has_value() == ammo.has_value()) {
                    file.problems.push_back(describe(section, index, "needs either caliber or ammo"));
                    continue;
                }
                if (caliber) {
                    target.caliber = *caliber;
                } else if (!parseForm(*ammo, target.ammoPlugin, target.ammoLocal)) {
                    file.problems.push_back(describe(section, index, "ammo must look like \"Plugin.esp|01F276\""));
                    continue;
                }
                rows.push_back(std::move(target));
            }
        }
    }

    [[nodiscard]] inline File parse(std::string_view text)
    {
        File file;
        toml::table root;
        try {
            root = toml::parse(text);
        } catch (const toml::parse_error& error) {
            file.problems.push_back("line " + std::to_string(error.source().begin.line) + ": " + std::string{ error.description() });
            return file;
        }
        for (const auto& [key, value] : root) {
            const auto name = key.str();
            if (name != "weapon" && name != "part" && name != "round" && name != "damage" && name != "default") {
                file.problems.push_back("unknown section " + std::string{ name } + ": use weapon, part, round, damage or default");
            } else if (!value.is_array_of_tables()) {
                file.problems.push_back(std::string{ name } + " must be written [[" + std::string{ name } + "]]");
            }
        }
        detail::readTargets(root, "weapon", file.weapons, file);
        detail::readTargets(root, "part", file.parts, file);
        if (const auto* array = root["round"].as_array()) {
            for (std::size_t index = 0; index < array->size(); ++index) {
                const auto* row = array->get(index)->as_table();
                RoundRow round;
                if (!row || !detail::readId(*row, "round", index, file, round.local)) {
                    continue;
                }
                const auto caliber = (*row)["caliber"].value<std::string_view>();
                if (!caliber) {
                    file.problems.push_back(detail::describe("round", index, "needs a caliber"));
                    continue;
                }
                round.caliber = *caliber;
                file.rounds.push_back(std::move(round));
            }
        }
        if (const auto* array = root["damage"].as_array()) {
            for (std::size_t index = 0; index < array->size(); ++index) {
                const auto* row = array->get(index)->as_table();
                DamageRow damage;
                if (!row || !detail::readId(*row, "damage", index, file, damage.local)) {
                    continue;
                }
                const auto value = (*row)["damage"].value<std::int64_t>();
                if (!value || *value < 1 || *value > 65535) {
                    file.problems.push_back(detail::describe("damage", index, "damage must be a whole number from 1 to 65535"));
                    continue;
                }
                damage.damage = static_cast<std::uint16_t>(*value);
                file.damage.push_back(damage);
            }
        }
        if (const auto* array = root["default"].as_array()) {
            for (std::size_t index = 0; index < array->size(); ++index) {
                const auto* row = array->get(index)->as_table();
                DefaultRow swap;
                if (!row || !detail::readId(*row, "default", index, file, swap.local)) {
                    continue;
                }
                const auto from = (*row)["from"].value<std::string_view>();
                const auto to = (*row)["to"].value<std::string_view>();
                if (!from || !to || !parseForm(*from, swap.fromPlugin, swap.fromLocal) || !parseForm(*to, swap.toPlugin, swap.toLocal)) {
                    file.problems.push_back(detail::describe("default", index, "from and to must look like \"Plugin.esp|003607\""));
                    continue;
                }
                file.defaults.push_back(std::move(swap));
            }
        }
        return file;
    }
}
