#pragma once

#include <RPS/Settings/Store.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// The mod's own settings file, Flying_BRICKS.toml, read once at startup through
// RPS Framework's settings store. A missing file or key means the default; the
// first start migrates Flying_BRICKS.ini of earlier versions and deletes it.
// Runs on the game's main thread.
namespace flying_bricks::settings
{
    enum class Force : std::int32_t
    {
        WeaponsOfFate = 0,
        Momentum = 1,
        Vanilla = 2,
    };
    inline constexpr std::int32_t kForceCount = 3;

    struct Values
    {
        bool damageModel{ true };
        Force force{ Force::WeaponsOfFate };
        bool playerHeadshotInstantKill{ true };
        bool hitLabels{ false };
        bool hitTrace{ false };
    };

    inline constexpr std::string_view kFileName = "Flying_BRICKS.toml";

    [[nodiscard]] std::unique_ptr<RPS::Settings::Store> makeStore(const std::filesystem::path& directory);
    // The loaded values; a choice out of range keeps its default and is reported.
    [[nodiscard]] Values fromStore(const RPS::Settings::Store& store, std::vector<std::string>& report);
    // Loads the player's file; report receives what the load did and every problem.
    [[nodiscard]] Values load(std::vector<std::string>& report);
}
