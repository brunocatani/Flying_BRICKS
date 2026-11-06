#pragma once

#include "PCH.h"

// Finds a form by plugin name and local ID, for full and light plugins. CommonLib's lookup goes through a compiled
// file collection that is not available on this VR stack, which leaves it blind to light plugins, so this reads
// each file's own compile indices.
namespace flying_bricks::plugin_forms
{
    inline constexpr std::uint8_t kNotLoaded = 0xFF;
    inline constexpr std::uint8_t kLightSlot = 0xFE;

    [[nodiscard]] inline const RE::TESFile* findLoaded(RE::TESDataHandler& dataHandler, std::string_view plugin) noexcept
    {
        for (const auto* file : dataHandler.files) {
            if (file && file->GetCompileIndex() != kNotLoaded && _strnicmp(file->GetFilename().data(), plugin.data(), plugin.size()) == 0 &&
                file->GetFilename().size() == plugin.size()) {
                return file;
            }
        }
        return nullptr;
    }

    template <class T>
    [[nodiscard]] T* find(RE::TESDataHandler& dataHandler, std::uint32_t local, std::string_view plugin) noexcept
    {
        const auto* file = findLoaded(dataHandler, plugin);
        if (!file) {
            return nullptr;
        }
        // A light plugin only holds IDs up to FFF. A larger one belongs to the plugin's full version, and masking it would
        // find an unrelated record.
        if (file->GetCompileIndex() == kLightSlot && local > 0xFFF) {
            return nullptr;
        }
        const auto formID = file->GetCompileIndex() == kLightSlot ?
                                (static_cast<std::uint32_t>(kLightSlot) << 24) | (static_cast<std::uint32_t>(file->GetSmallFileCompileIndex()) << 12) | (local & 0xFFF) :
                                (static_cast<std::uint32_t>(file->GetCompileIndex()) << 24) | (local & 0xFFFFFF);
        return RE::TESForm::GetFormByID<T>(formID);
    }
}
