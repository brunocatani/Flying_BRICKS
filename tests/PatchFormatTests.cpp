#include "PatchFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <sstream>

// Guards the patch file format, a public contract other mods write to, and that every file the mod ships reads cleanly.
namespace
{
    using flying_bricks::patch_format::CaliberRow;
    namespace format = flying_bricks::patch_format;

#include "CaliberData.inl"

    int s_failures = 0;

    void check(bool passed, const char* what)
    {
        if (!passed) {
            std::printf("FAILED: %s\n", what);
            ++s_failures;
        }
    }

    void testRows()
    {
        const auto file = format::parse(R"(
[[weapon]]
id = "19000F9B"
caliber = "9x19"
name = "Glock 19x"

[[weapon]]
id = "FE012F9B"
ammo = "Fallout4.esm|01F276"

[[part]]
id = "0x12AB"
caliber = "45acp"

[[round]]
id = "000FA8"
caliber = "9x19"

[[damage]]
id = "000805"
damage = 36

[[default]]
id = "000F99"
from = "A.esp|003607"
to = "B.esm|003606"
)");
        check(file.problems.empty(), "a good file has no problems");
        check(file.defaults.size() == 1 && file.defaults[0].fromPlugin == "A.esp" && file.defaults[0].fromLocal == 0x3607 && file.defaults[0].toPlugin == "B.esm" && file.defaults[0].toLocal == 0x3606, "a default part row is read");
        check(format::parse("[[default]]\nid = \"1\"\nfrom = \"A.esp|1\"\n").problems.size() == 1, "a default part row needs both parts");
        check(file.weapons.size() == 2 && file.parts.size() == 1 && file.rounds.size() == 1 && file.damage.size() == 1, "every row is read");
        check(file.weapons[0].local == 0xF9B, "a load order prefix is dropped");
        check(file.weapons[1].local == 0xF9B, "a light plugin's prefix and slot are dropped");
        check(file.weapons[1].ammoPlugin == "Fallout4.esm" && file.weapons[1].ammoLocal == 0x1F276 && file.weapons[1].caliber.empty(), "an exact round is read");
        check(file.parts[0].local == 0x12AB && file.parts[0].caliber == "45acp", "a part row is read");
        check(file.damage[0].damage == 36, "a damage row is read");
    }

    void testBadRows()
    {
        check(format::parse("this is not toml").problems.size() == 1, "broken TOML is a problem, not an exception");
        check(!format::parse("[[weapons]]\nid = \"1\"\ncaliber = \"9x19\"\n").problems.empty(), "a misspelled section is reported");
        check(!format::parse("[weapon]\nid = \"1\"\ncaliber = \"9x19\"\n").problems.empty(), "a single bracket section is reported");

        const auto file = format::parse(R"(
[[weapon]]
id = "nothex"
caliber = "9x19"

[[weapon]]
id = "000001"

[[weapon]]
id = "000002"
caliber = "9x19"
ammo = "Fallout4.esm|01F276"

[[weapon]]
id = "000003"
ammo = "Fallout4.esm"

[[weapon]]
id = "000004"
caliber = "9x19"

[[damage]]
id = "000005"
damage = 0
)");
        check(file.problems.size() == 5, "each bad row is reported");
        check(file.weapons.size() == 1 && file.weapons[0].local == 4, "a bad row does not cost the good ones");
        check(file.damage.empty(), "zero damage is refused");
    }

    bool knownCaliber(std::string_view id)
    {
        return std::ranges::find(kCaliberRows, id, &CaliberRow::id) != kCaliberRows.end();
    }

    void testShippedFiles()
    {
        std::size_t files = 0;
        for (const auto& entry : std::filesystem::directory_iterator{ PATCH_FOLDER }) {
            ++files;
            const auto name = entry.path().filename().string();
            auto plugin = entry.path().stem().extension().string();  // Plugin names keep their author's case, such as .ESP.
            std::ranges::transform(plugin, plugin.begin(), [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
            check(entry.path().extension() == ".toml" && (plugin == ".esp" || plugin == ".esm" || plugin == ".esl"), name.c_str());
            std::ifstream stream{ entry.path(), std::ios::binary };
            std::ostringstream text;
            text << stream.rdbuf();
            const auto file = format::parse(text.str());
            for (const auto& problem : file.problems) {
                std::printf("%s: %s\n", name.c_str(), problem.c_str());
                ++s_failures;
            }
            for (const auto& row : file.weapons) {
                check(row.caliber.empty() || knownCaliber(row.caliber), name.c_str());
            }
            for (const auto& row : file.parts) {
                check(row.caliber.empty() || knownCaliber(row.caliber), name.c_str());
            }
            for (const auto& row : file.rounds) {
                check(knownCaliber(row.caliber), name.c_str());
            }
        }
        check(files > 0, "the mod ships patch files");
    }
}

int main()
{
    testRows();
    testBadRows();
    testShippedFiles();
    return s_failures == 0 ? 0 : 1;
}
