// Flying_BRICKS.toml contracts: a fresh file gives the compiled defaults, a
// choice out of range keeps its default and is reported, and the INI of earlier
// versions carries the player's changes over once and is deleted.
#include "Settings.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace
{
    namespace fs = std::filesystem;
    using namespace flying_bricks::settings;

    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::fprintf(stderr, "%s\n", message);
            std::exit(1);
        }
    }
    void write(const fs::path& path, const char* text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
    }
}

int main()
{
    const fs::path root(SETTINGS_TEST_DIR);
    std::error_code ignored;
    fs::remove_all(root, ignored);

    const auto fresh = root / "fresh";
    fs::create_directories(fresh);
    auto store = makeStore(fresh);
    store->load(true);
    require(store->messages().empty() && fs::exists(fresh / kFileName), "fresh file was not written cleanly");
    std::vector<std::string> report;
    const auto defaults = fromStore(*store, report);
    const Values compiled;
    require(report.empty() && defaults.damageModel == compiled.damageModel && defaults.force == compiled.force && defaults.playerHeadshotInstantKill == compiled.playerHeadshotInstantKill &&
            defaults.hitLabels == compiled.hitLabels && defaults.hitTrace == compiled.hitTrace,
        "documented defaults differ from the compiled defaults");

    write(fresh / kFileName, "[General]\nForce = 7\n");
    store->load(false);
    const auto ranged = fromStore(*store, report);
    require(ranged.force == compiled.force && report.size() == 1,
        "an out-of-range choice must keep its default and be reported");

    // The player's changed values carry over; a value equal to the old default
    // follows the current default.
    const auto upgrade = root / "upgrade";
    fs::create_directories(upgrade);
    write(upgrade / "Flying_BRICKS.ini", "[General]\r\nbDamageModel=0\r\niFlight=2\r\niForce=2\r\n[Debug]\r\nbHitTrace=1\r\nbHitLabels=0");
    store = makeStore(upgrade);
    store->load(true);
    require(!fs::exists(upgrade / "Flying_BRICKS.ini"), "the INI was not deleted after migration");
    report.clear();
    const auto migrated = fromStore(*store, report);
    require(!migrated.damageModel && migrated.force == Force::Vanilla &&
            migrated.hitTrace && !migrated.hitLabels && migrated.playerHeadshotInstantKill,
        "migrated values differ");

    fs::remove_all(root, ignored);
    return 0;
}
