#include "PCH.h"

#include "AmmoTable.h"
#include "Compatibility.h"
#include "HitLabels.h"
#include "Hooks.h"
#include "Melee.h"
#include "PatchFiles.h"
#include "Presets.h"
#include "Settings.h"
#include "Terminal.h"

#include <ShlObj.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    // 58 call patches at 14 bytes each.
    constexpr std::size_t kTrampolineSize = 1024;

    void initializeLog()
    {
        wchar_t documents[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documents))) {
            throw std::runtime_error("Documents folder is unavailable");
        }
        const auto directory = std::filesystem::path(documents) / L"My Games" / L"Fallout4VR" / L"F4SE";
        std::filesystem::create_directories(directory);
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((directory / L"Flying_BRICKS.log").string(), true);
        auto logger = std::make_shared<spdlog::logger>("Flying_BRICKS", std::move(sink));
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    }

    void applySettings()
    {
        using namespace flying_bricks;

        std::vector<std::string> report;
        settings::Values values;
        try {
            values = settings::load(report);
        } catch (const std::exception& error) {
            spdlog::error("Settings unavailable, using defaults: {}", error.what());
        }
        for (const auto& line : report) {
            spdlog::info("Settings: {}", line);
        }
        spdlog::info(
            "Settings: DamageModel={} Force={} HitLabels={} HitTrace={}",
            values.damageModel,
            static_cast<std::int32_t>(values.force),
            values.hitLabels,
            values.hitTrace);
        terminal::setPlayerHeadshotInstantKill(values.playerHeadshotInstantKill);
        hooks::setDamageModelActive(values.damageModel);
        presets::apply(values.force);
        hit_labels::setEnabled(values.hitLabels);
        hooks::setHitTrace(values.hitTrace);
    }

    // Fails closed at each stage: without a clean preflight or any owned ammunition the game keeps
    // every native damage path, and the ESP's records load as plain data.
    void startRuntime()
    {
        using namespace flying_bricks;

        if (!hooks::preflight()) {
            spdlog::critical("Hooks were not installed; all damage stays native");
            return;
        }
        spdlog::info("Preflight passed");

        const auto armor = terminal::build();
        spdlog::info("Armor: {} by template, {} by rating, {} heavy mods, {} weave mods, {} creature races", armor.armorRows, armor.armorByRating, armor.heavyMods, armor.weaveMods, armor.creatures);

        melee::build();

        const auto patches = patch_files::load();
        spdlog::info("Patch files: {} read, {} for plugins not loaded, {} problems, {} rows name a form the plugin does not have", patches.filesRead, patches.filesNotLoaded, patches.problems, patches.formsNotFound);

        const auto ammo = ammo_table::build(patches.rounds);
        spdlog::info("Ammo table: {} owned, {} from other mods, {} overridden by a later plugin", ammo.owned, ammo.foreign, ammo.overriddenByLaterPlugin);
        if (ammo.owned == 0) {
            spdlog::warn("No ammunition is owned by Flying_BRICKS.esp; hooks were not installed and all damage stays native");
            return;
        }
        const auto lent = compatibility::apply(patches);
        spdlog::info(
            "Compatibility: {} providers, {} models set; foreign ammo replaced in {} weapons, {} object mods, {} leveled entries, {} form list entries, {} recipe entries, {} container entries",
            lent.providers, lent.models, lent.weapons, lent.objectMods, lent.leveledLists, lent.formLists, lent.recipes, lent.containers);
        const auto projectiles = presets::classify();
        spdlog::info("Projectiles: {} classified, {} unknown", projectiles.classified, projectiles.unknown);

        // Settings and presets go live before the first patch, so no shot sees a half-applied state.
        hit_labels::connect();
        applySettings();
        if (!hooks::install()) {
            spdlog::critical("Hook installation failed; see the error above");
        }
    }

    void F4SEAPI onMessage(F4SE::MessagingInterface::Message* message) noexcept
    {
        if (!message) {
            return;
        }
        try {
            static bool s_started = false;  // F4SE delivers messages on one thread.
            if (message->type == F4SE::MessagingInterface::kGameLoaded && !s_started) {
                s_started = true;
                startRuntime();
            } else if (message->type == F4SE::MessagingInterface::kPostLoadGame || message->type == F4SE::MessagingInterface::kNewGame) {
                flying_bricks::hit_labels::connect();
                flying_bricks::compatibility::warnIfIncompatible();
            }
        } catch (const std::exception& error) {
            spdlog::error("Message handling failed: {}", error.what());
        } catch (...) {
            spdlog::error("Message handling failed: unknown exception");
        }
    }
}

// Query rejects the editor and anything but Fallout4VR.exe 1.2.72.0 (the module version, never
// QueryInterface::RuntimeVersion(), which reports F4SEVR's flat-compatibility value).
extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(
    const F4SE::QueryInterface* query, F4SE::PluginInfo* info) noexcept
{
    try {
        if (!query || !info) {
            return false;
        }
        initializeLog();
        info->infoVersion = F4SE::PluginInfo::kVersion;
        info->name = "Flying_BRICKS";
        info->version = 1;
        if (query->IsEditor() || !REL::Module::IsVR() ||
            REL::Module::get().version() != F4SE::RUNTIME_VR_1_2_72) {
            spdlog::error("Fallout4VR.exe 1.2.72.0 is required");
            return false;
        }
        spdlog::info("Query complete");
        return true;
    } catch (...) {
        return false;
    }
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* load) noexcept
{
    try {
        if (!load) {
            return false;
        }
        F4SE::Init(load, false);
        F4SE::AllocTrampoline(kTrampolineSize);
        const auto* messaging = F4SE::GetMessagingInterface();
        if (!messaging || !messaging->RegisterListener(onMessage)) {
            spdlog::error("F4SE message registration failed");
            return false;
        }
        spdlog::info("Load complete");
        return true;
    } catch (...) {
        return false;
    }
}
