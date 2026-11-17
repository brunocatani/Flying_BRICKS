#include "PCH.h"

#include "Compatibility.h"

#include "AmmoTable.h"
#include "PatchFiles.h"
#include "PluginForms.h"

#include <RE/Bethesda/SendPapyrusEvent.h>

namespace flying_bricks::compatibility
{
    namespace
    {
        // Set by apply() and read by warnIfIncompatible(), both on the F4SE message thread.
        bool s_incompatibleLoaded = false;

        struct ProviderRow
        {
            std::string_view plugin;
            std::string_view sampleMesh;  // Relative to Meshes; a plugin alone does not prove its assets exist.
        };

        struct VisualRow
        {
            std::uint32_t normalProjectile;
            std::string_view armamentFlight;
            std::string_view armamentBox;
            std::string_view armamentCasing;
            bool armamentExact;
            std::string_view munitionsFlight;
            std::string_view munitionsBox;
            std::string_view munitionsCasing;
            std::string_view wofNormal;
            std::string_view wofSuppressed;
            std::string_view bibfxNormal;
            std::string_view bibfxSuppressed;
            bool bibfxImpact;  // BIBFX's own shotgun projectiles have no impact explosion, so pellets get none.
        };

        struct FormRow
        {
            std::string_view plugin;
            std::uint32_t local;
        };

        struct LootReference
        {
            std::string_view plugin;
            std::uint32_t list;
            float vanillaMean;  // Mean count of the list's entries in the master that defines it.
        };

#include "CompatibilityData.inl"

        // BGSProjectile flags and the object mod property holding a weapon's ammunition.
        constexpr std::uint32_t kProjectileAltTrigger = 1u << 2;
        constexpr std::uint32_t kAmmoProperty = 61;
        // TESLeveledList::llFlags holds the record's LVLF flags as written: GetMultCalc (vtable slot 8) returns bit 0x02 of
        // the byte at +0x2B, and the constructor zeroes that byte with the other byte fields at +0x28..+0x2C.
        constexpr std::int8_t kLeveledUseAll = 0x04;

        struct Active
        {
            bool munitions{};
            bool armament{};
            bool bibfx{};
            bool wof{};
        };

        [[nodiscard]] bool isActive(RE::TESDataHandler& dataHandler, const ProviderRow& provider)
        {
            if (!plugin_forms::findLoaded(dataHandler, provider.plugin)) {
                return false;
            }
            // Weapons of Fate Redux ships the plugin without the meshes, so the plugin alone is not enough.
            const std::filesystem::path plugin{ provider.plugin };
            std::error_code error;
            const std::filesystem::path data{ "Data" };
            const auto present = std::filesystem::exists(data / (plugin.stem().string() + " - Main.ba2"), error) ||
                                 std::filesystem::exists(data / "Meshes" / provider.sampleMesh, error);
            if (!present) {
                spdlog::warn("{} is loaded but its meshes were not found; it provides nothing", provider.plugin);
            }
            return present;
        }

        // An exact model beats a close caliber's, whichever mod holds it, then the higher mod wins.
        [[nodiscard]] std::string_view choose(const Active& active, bool exact, std::string_view armament, std::string_view munitions) noexcept
        {
            if (active.armament && exact && !armament.empty()) {
                return armament;
            }
            if (active.munitions && !munitions.empty()) {
                return munitions;
            }
            return active.armament ? armament : std::string_view{};
        }

        [[nodiscard]] std::string_view chooseFlight(const Active& active, const VisualRow& row, bool suppressed) noexcept
        {
            if (active.wof) {
                return suppressed ? row.wofSuppressed : row.wofNormal;
            }
            if (active.bibfx) {
                return suppressed ? row.bibfxSuppressed : row.bibfxNormal;
            }
            return choose(active, row.armamentExact, row.armamentFlight, row.munitionsFlight);
        }

        [[nodiscard]] bool setModel(RE::TESModel& model, std::string_view path)
        {
            if (path.empty()) {
                return false;
            }
            model.model = path.data();  // Generated literals, so null terminated.
            return true;
        }

        [[nodiscard]] const VisualRow* findVisual(const RE::BGSProjectile& normal) noexcept
        {
            const auto local = plugin_forms::localOf(normal.GetFormID());
            const auto row = std::ranges::lower_bound(kVisualRows, local, {}, &VisualRow::normalProjectile);
            return row != kVisualRows.end() && row->normalProjectile == local ? std::to_address(row) : nullptr;
        }

        [[nodiscard]] RE::BGSExplosion* findImpact(RE::TESDataHandler& dataHandler, std::uint32_t local)
        {
            auto* explosion = plugin_forms::find<RE::BGSExplosion>(dataHandler, local, kBibfxPlugin);
            if (!explosion) {
                spdlog::warn("BIBFX explosion {:06X} was not found; its impact effect is skipped", local);
                return nullptr;
            }
            // Our model is the only source of bullet damage.
            if (explosion->data.damage != 0.0F) {
                spdlog::info("BIBFX explosion {:06X} deals {} damage; set to 0", local, explosion->data.damage);
                explosion->data.damage = 0.0F;
            }
            return explosion;
        }

        void applyImpact(RE::BGSProjectile* projectile, RE::BGSExplosion* explosion) noexcept
        {
            if (projectile && explosion) {
                projectile->data.explosionType = explosion;
                projectile->data.flags |= kProjectileAltTrigger;
            }
        }

        std::size_t applyVisuals(RE::TESDataHandler& dataHandler, const Active& active)
        {
            auto* normalImpact = active.bibfx ? findImpact(dataHandler, kBibfxNormalExplosion) : nullptr;
            auto* suppressedImpact = active.bibfx ? findImpact(dataHandler, kBibfxSuppressedExplosion) : nullptr;

            std::size_t models = 0;
            for (const auto& entry : ammo_table::entries()) {
                if (entry.foreign || !entry.normal) {
                    continue;
                }
                const auto* row = findVisual(*entry.normal);
                if (!row) {
                    spdlog::warn("Projectile {:08X} has no visual row and keeps its vanilla models", entry.normal->GetFormID());
                    continue;
                }
                models += setModel(*entry.ammo, choose(active, row->armamentExact, row->armamentBox, row->munitionsBox)) ? 1 : 0;
                models += setModel(entry.ammo->shellCasing, choose(active, row->armamentExact, row->armamentCasing, row->munitionsCasing)) ? 1 : 0;
                models += setModel(*entry.normal, chooseFlight(active, *row, false)) ? 1 : 0;
                if (row->bibfxImpact) {
                    applyImpact(entry.normal, normalImpact);
                }
                if (entry.suppressed) {
                    models += setModel(*entry.suppressed, chooseFlight(active, *row, true)) ? 1 : 0;
                    if (row->bibfxImpact) {
                        applyImpact(entry.suppressed, suppressedImpact);
                    }
                }
            }
            return models;
        }

        // Foreign ammunition to ours, sorted by the foreign record.
        class Replacements
        {
        public:
            Replacements()
            {
                for (const auto& entry : ammo_table::entries()) {
                    if (entry.foreign && entry.replacement) {
                        _rows.emplace_back(entry.ammo, entry.replacement);
                    }
                }
            }

            [[nodiscard]] bool empty() const noexcept { return _rows.empty(); }

            [[nodiscard]] RE::TESAmmo* find(const RE::TESAmmo* ammo) const noexcept
            {
                if (!ammo) {
                    return nullptr;
                }
                const auto row = std::ranges::lower_bound(_rows, ammo, {}, &std::pair<RE::TESAmmo*, RE::TESAmmo*>::first);
                return row != _rows.end() && row->first == ammo ? row->second : nullptr;
            }

            template <class T>
            [[nodiscard]] bool swap(T*& form) const noexcept
            {
                auto* replacement = form ? find(form->template As<RE::TESAmmo>()) : nullptr;
                if (!replacement) {
                    return false;
                }
                form = replacement;
                return true;
            }

        private:
            std::vector<std::pair<RE::TESAmmo*, RE::TESAmmo*>> _rows;
        };

        // CommonLib declares the leveled list counts as signed, but a list holds up to 255 entries, and one with more than
        // 127 would read as negative.
        [[nodiscard]] std::size_t entryCount(std::int8_t count)
        {
            return static_cast<std::uint8_t>(count);
        }

        // Rewrites everything that names foreign ammunition. Patches from any mod, loaded as ESPs or applied by
        // RobCo Patcher before this runs, end up on our records without knowing them.
        void applyReplacements(RE::TESDataHandler& dataHandler, const Replacements& replacements, ApplyResult& result)
        {
            for (auto* weapon : dataHandler.GetFormArray<RE::TESObjectWEAP>()) {
                if (weapon && replacements.swap(weapon->weaponData.ammo)) {
                    ++result.weapons;
                }
            }
            for (auto* objectMod : dataHandler.GetFormArray<RE::BGSMod::Attachment::Mod>()) {
                if (!objectMod) {
                    continue;
                }
                for (auto& property : objectMod->GetBuffer<RE::BGSMod::Property::Mod>(1)) {
                    if (property.target == kAmmoProperty && property.type == RE::BGSMod::Property::TYPE::kForm && replacements.swap(property.data.form)) {
                        ++result.objectMods;
                    }
                }
            }
            for (auto* leveledItem : dataHandler.GetFormArray<RE::TESLevItem>()) {
                if (!leveledItem) {
                    continue;
                }
                auto& list = static_cast<RE::TESLeveledList&>(*leveledItem);
                if (list.leveledLists) {
                    const auto count = entryCount(list.baseListCount);
                    for (std::size_t index = 0; index < count; ++index) {
                        result.leveledLists += replacements.swap(list.leveledLists[index].form) ? 1 : 0;
                    }
                }
                if (list.scriptAddedLists) {
                    const auto count = entryCount(list.scriptListCount);
                    for (std::size_t index = 0; index < count; ++index) {
                        auto* entry = list.scriptAddedLists[index];
                        result.leveledLists += entry && replacements.swap(entry->form) ? 1 : 0;
                    }
                }
            }
            for (auto* formList : dataHandler.GetFormArray<RE::BGSListForm>()) {
                if (!formList) {
                    continue;
                }
                for (auto*& form : formList->arrayOfForms) {
                    result.formLists += replacements.swap(form) ? 1 : 0;
                }
                if (formList->scriptAddedTempForms) {
                    for (auto& formID : *formList->scriptAddedTempForms) {
                        if (auto* replacement = replacements.find(RE::TESForm::GetFormByID<RE::TESAmmo>(formID))) {
                            formID = replacement->GetFormID();
                            ++result.formLists;
                        }
                    }
                }
            }
            for (auto* recipe : dataHandler.GetFormArray<RE::BGSConstructibleObject>()) {
                if (!recipe) {
                    continue;
                }
                result.recipes += replacements.swap(recipe->createdItem) ? 1 : 0;
                if (recipe->requiredItems) {
                    for (auto& required : *recipe->requiredItems) {
                        result.recipes += replacements.swap(required.first) ? 1 : 0;
                    }
                }
            }
            const auto swapContainer = [&](RE::TESContainer& container) {
                if (!container.containerObjects) {
                    return;
                }
                for (std::uint32_t index = 0; index < container.numContainerObjects; ++index) {
                    auto* entry = container.containerObjects[index];
                    result.containers += entry && replacements.swap(entry->obj) ? 1 : 0;
                }
            };
            for (auto* container : dataHandler.GetFormArray<RE::TESObjectCONT>()) {
                if (container) {
                    swapContainer(*container);
                }
            }
            for (auto* npc : dataHandler.GetFormArray<RE::TESNPC>()) {
                if (npc) {
                    swapContainer(*npc);
                }
            }
        }

        // Sets each patched weapon and object mod to its round whatever is there now, so a patch from another mod that
        // already changed it, RobCo Patcher's included, cannot win. Runs after applyReplacements for the same reason.
        void applyWeaponPatches(const patch_files::Patches& patches, ApplyResult& result)
        {
            for (const auto& row : patches.weapons) {
                row.weapon->weaponData.ammo = row.ammo;
                if (row.ammoList) {
                    row.weapon->weaponData.npcAddAmmoList = row.ammoList;
                }
                ++result.patchedWeapons;
            }
            for (const auto& row : patches.parts) {
                for (auto& property : row.part->GetBuffer<RE::BGSMod::Property::Mod>(1)) {
                    if (property.target == kAmmoProperty && property.type == RE::BGSMod::Property::TYPE::kForm) {
                        property.data.form = row.ammo;
                        ++result.patchedObjectMods;
                    }
                }
            }
            // A gun can spawn with a part whose round is wrong for it, and that part is a fair choice on other guns, so its
            // round stays and only this gun's template changes. An entry's part pointer is replaced in place, keeping its
            // index and flags. Nothing is allocated, and spawned weapons read the entries when they are built, so a weapon
            // that already exists keeps its parts. Layout verified from the VR binary's raw disassembly: the template list
            // at +0x30 of the object, its items at +0x08 with the count at +0x18, each item's buffer at +0x10 with
            // attachments in block 0, and 0x10 bytes per entry with the part first.
            for (const auto& row : patches.defaults) {
                bool swapped = false;
                for (auto* item : row.weapon->objectTemplate.items) {
                    if (!item) {
                        continue;
                    }
                    for (auto& attachment : item->GetBuffer<RE::BGSMod::Attachment::Instance>(0)) {
                        if (attachment.mod == row.from) {
                            attachment.mod = row.to;
                            swapped = true;
                        }
                    }
                }
                if (swapped) {
                    ++result.swappedDefaults;
                } else {
                    spdlog::info("Patch files: weapon {:08X} does not spawn with part {:08X}; nothing to swap", row.weapon->GetFormID(), row.from->GetFormID());
                }
            }
            // Armament zeroes every ballistic gun's damage and moves it to the rounds it lists. A gun on any other round,
            // such as nails or arrows, would do nothing, so it gets its authored damage back. Only a zero is undone.
            for (const auto& row : patches.damage) {
                if (row.weapon->weaponData.attackDamage == 0) {
                    row.weapon->weaponData.attackDamage = row.damage;
                    ++result.restoredDamage;
                }
            }
        }

        // Fills our world list with the rounds loaded guns and parts fire, so a caliber nothing fires never drops. A round
        // more guns use gets more slots, up to four. The list only shrinks in place: its array keeps the engine's own
        // allocation and the slots past the new count stay valid, so nothing here allocates or frees engine memory.
        // RobCo Patcher adds the list to the game's vendor and loot lists; a host without it is logged.
        void applyWorldAmmo(RE::TESDataHandler& dataHandler, ApplyResult& result)
        {
            auto* world = plugin_forms::find<RE::TESLevItem>(dataHandler, kWorldAmmoList, kOurPlugin);
            if (!world || !world->leveledLists || entryCount(world->baseListCount) == 0) {
                spdlog::warn("World ammo: list {:06X} is missing or empty", kWorldAmmoList);
                return;
            }
            auto& list = static_cast<RE::TESLeveledList&>(*world);

            struct Live
            {
                RE::TESLevItem* list{};
                std::size_t weapons{};
                bool fired{};
            };
            std::unordered_map<const RE::TESForm*, Live> rounds;
            rounds.reserve(patch_files::calibers().size());
            for (const auto& row : patch_files::calibers()) {
                if (!row.worldList) {
                    continue;
                }
                auto* ammo = plugin_forms::find<RE::TESAmmo>(dataHandler, row.local, row.plugin);
                auto* ammoList = plugin_forms::find<RE::TESLevItem>(dataHandler, row.ammoList, kOurPlugin);
                if (ammo && ammoList) {
                    rounds.emplace(ammo, Live{ ammoList });
                }
            }
            for (const auto* weapon : dataHandler.GetFormArray<RE::TESObjectWEAP>()) {
                if (const auto found = weapon ? rounds.find(weapon->weaponData.ammo) : rounds.end(); found != rounds.end()) {
                    found->second.fired = true;
                    ++found->second.weapons;
                }
            }
            for (auto* objectMod : dataHandler.GetFormArray<RE::BGSMod::Attachment::Mod>()) {
                if (!objectMod) {
                    continue;
                }
                for (auto& property : objectMod->GetBuffer<RE::BGSMod::Property::Mod>(1)) {
                    if (property.target == kAmmoProperty && property.type == RE::BGSMod::Property::TYPE::kForm) {
                        if (const auto found = rounds.find(property.data.form); found != rounds.end()) {
                            found->second.fired = true;
                        }
                    }
                }
            }

            const auto capacity = entryCount(list.baseListCount);
            std::size_t used = 0;
            std::size_t dropped = 0;
            for (const auto& [ammo, live] : rounds) {
                if (live.fired) {
                    ++result.worldCalibers;
                }
            }
            // Every caliber gets one slot before any gets a second, so a full list loses weight and never a round.
            for (std::size_t pass = 0; pass < 4; ++pass) {
                for (const auto& [ammo, live] : rounds) {
                    if (!live.fired || (pass > 0 && live.weapons < pass * 5)) {
                        continue;
                    }
                    if (used == capacity) {
                        dropped += pass == 0 ? 1 : 0;
                        continue;
                    }
                    list.leveledLists[used++] = RE::LEVELED_OBJECT{ live.list, nullptr, 1, 1, 0 };
                }
            }
            list.baseListCount = static_cast<std::int8_t>(used);
            if (dropped > 0) {
                spdlog::warn("World ammo: {} calibers did not fit in the {} slots", dropped, capacity);
            }

            const auto* host = RE::TESForm::GetFormByID<RE::TESLevItem>(kWorldAmmoHost);
            bool hosted = false;
            if (host && host->leveledLists) {
                const auto hostCount = entryCount(host->baseListCount);
                for (std::size_t index = 0; index < hostCount && !hosted; ++index) {
                    hosted = host->leveledLists[index].form == world;
                }
            }
            if (hosted) {
                spdlog::info("World ammo: {} calibers in {} slots", result.worldCalibers, used);
            } else {
                spdlog::warn("World ammo: {} calibers in {} slots, but the list is not in the game's lists. Is RobCo Patcher installed?", result.worldCalibers, used);
            }
        }

        // Whether a form comes from this file, by its form ID and the file's compile indices (see plugin_forms::find).
        [[nodiscard]] bool fromFile(const RE::TESForm& form, const RE::TESFile& file) noexcept
        {
            const auto id = form.GetFormID();
            if (file.GetCompileIndex() == plugin_forms::kLightSlot) {
                return (id >> 24) == plugin_forms::kLightSlot && ((id >> 12) & 0xFFF) == file.GetSmallFileCompileIndex();
            }
            return (id >> 24) == file.GetCompileIndex();
        }

        // The item of ours that stands for each caliber, after applyReplacements put ours in every list.
        [[nodiscard]] std::unordered_map<const RE::TESForm*, const patch_format::CaliberRow*> caliberRounds(RE::TESDataHandler& dataHandler)
        {
            std::unordered_map<const RE::TESForm*, const patch_format::CaliberRow*> rounds;
            rounds.reserve(patch_files::calibers().size());
            for (const auto& row : patch_files::calibers()) {
                if (const auto* ammo = plugin_forms::find<RE::TESAmmo>(dataHandler, row.local, row.plugin)) {
                    rounds.emplace(ammo, &row);
                }
            }
            return rounds;
        }

        // The one round every entry of a list gives, or none.
        [[nodiscard]] const RE::TESForm* singleRound(const RE::TESLeveledList& list) noexcept
        {
            const auto count = entryCount(list.baseListCount);
            if (!list.leveledLists || count == 0) {
                return nullptr;
            }
            const auto* round = list.leveledLists[0].form;
            for (std::size_t index = 1; index < count; ++index) {
                if (list.leveledLists[index].form != round) {
                    return nullptr;
                }
            }
            return round;
        }

        // A loot overhaul tunes the game's own ammo lists and cannot know ours or the rounds we added. Those lists follow
        // its scale instead: our lists, and every list of a round we added, which after applyReplacements includes the
        // Munitions, Armament and weapon mod lists of a replaced round. A list's counts are scaled by the live mean count of
        // the game's list of its caliber's class over that list's mean in the base game, so without an overhaul the scale is
        // 1 and nothing changes. Lists of base game rounds from other mods stay as their author or the overhaul left them.
        // Counts change in place; no list grows or shrinks.
        void applyLootScale(RE::TESDataHandler& dataHandler, const std::unordered_map<const RE::TESForm*, const patch_format::CaliberRow*>& rounds, ApplyResult& result)
        {
            std::array<float, kLootReferences.size()> scales{};
            std::array<const RE::TESLevItem*, kLootReferences.size()> references{};
            bool scaled = false;
            for (std::size_t index = 0; index < kLootReferences.size(); ++index) {
                const auto& row = kLootReferences[index];
                scales[index] = 1.0F;
                const auto* list = plugin_forms::find<RE::TESLevItem>(dataHandler, row.list, row.plugin);
                const auto count = list && list->leveledLists ? entryCount(list->baseListCount) : 0;
                if (count == 0) {
                    spdlog::warn("Loot scale: {}|{:06X} is missing or empty; its calibers keep their counts", row.plugin, row.list);
                    continue;
                }
                references[index] = list;
                float total = 0.0F;
                for (std::size_t entry = 0; entry < count; ++entry) {
                    total += list->leveledLists[entry].count;
                }
                const auto mean = total / static_cast<float>(count);
                scales[index] = mean / row.vanillaMean;
                if (std::abs(scales[index] - 1.0F) > 0.01F) {
                    scaled = true;
                    spdlog::info("Loot scale: {}|{:06X} gives {:.2f} per pick against {:.2f} in the base game, scale {:.2f}", row.plugin, row.list, mean, row.vanillaMean, scales[index]);
                }
            }
            if (!scaled) {
                spdlog::info("Loot scale: the game's ammo lists are as in the base game; counts unchanged");
                return;
            }

            const auto* ours = plugin_forms::findLoaded(dataHandler, kOurPlugin);
            for (auto* leveledItem : dataHandler.GetFormArray<RE::TESLevItem>()) {
                if (!leveledItem || std::ranges::find(references, leveledItem) != references.end()) {
                    continue;
                }
                auto& list = static_cast<RE::TESLeveledList&>(*leveledItem);
                const auto found = rounds.find(singleRound(list));
                if (found == rounds.end()) {
                    continue;
                }
                const auto& caliber = *found->second;
                if (!ours || !(fromFile(*found->first, *ours) || fromFile(*leveledItem, *ours))) {
                    continue;
                }
                const auto scale = scales[caliber.lootReference];
                if (std::abs(scale - 1.0F) <= 0.01F) {
                    continue;
                }
                const auto count = entryCount(list.baseListCount);
                for (std::size_t index = 0; index < count; ++index) {
                    auto& entry = list.leveledLists[index];
                    entry.count = static_cast<std::uint16_t>(std::clamp(std::lround(entry.count * scale), 1L, 0xFFFFL));
                }
                ++result.scaledLists;
            }
            spdlog::info("Loot scale: {} lists of our rounds scaled", result.scaledLists);
        }

        // RobCo Patcher adds our world list and the providers' ammo lists to the hosts with fixed counts. Each host gets them
        // the way its own entries are written in this load order: one pick in a list that picks one entry, and none in a
        // use-all list that rolls no caliber list of its own, because the lists it rolls carry ours. Damn Apocalypse's weapon
        // store rolls only its basic and rare lists, which its drop-rate global gates, so our rounds stay behind that gate.
        // A host only shrinks in place, as applyWorldAmmo's list does.
        void applyHostShape(RE::TESDataHandler& dataHandler, const std::unordered_map<const RE::TESForm*, const patch_format::CaliberRow*>& rounds, ApplyResult& result)
        {
            std::vector<const RE::TESForm*> added;
            if (const auto* world = plugin_forms::find<RE::TESLevItem>(dataHandler, kWorldAmmoList, kOurPlugin)) {
                added.push_back(world);
            }
            for (const auto& row : kProviderAmmoLists) {
                if (const auto* list = plugin_forms::find<RE::TESLevItem>(dataHandler, row.local, row.plugin)) {
                    added.push_back(list);
                }
            }
            const auto isAdded = [&](const RE::TESForm* form) { return std::ranges::find(added, form) != added.end(); };
            // A caliber list: a round of ours, or a list whose entries are rounds of ours.
            const auto isCaliber = [&](const RE::TESForm* form) {
                if (rounds.contains(form)) {
                    return true;
                }
                const auto* list = form ? form->As<RE::TESLevItem>() : nullptr;
                if (!list || !list->leveledLists) {
                    return false;
                }
                const auto count = entryCount(list->baseListCount);
                for (std::size_t index = 0; index < count; ++index) {
                    if (rounds.contains(list->leveledLists[index].form)) {
                        return true;
                    }
                }
                return false;
            };

            for (const auto& row : kWorldAmmoHosts) {
                auto* host = plugin_forms::find<RE::TESLevItem>(dataHandler, row.local, row.plugin);
                if (!host || !host->leveledLists) {
                    continue;
                }
                auto& list = static_cast<RE::TESLeveledList&>(*host);
                const auto count = entryCount(list.baseListCount);
                if ((list.llFlags & kLeveledUseAll) == 0) {
                    for (std::size_t index = 0; index < count; ++index) {
                        auto& entry = list.leveledLists[index];
                        if (isAdded(entry.form) && entry.count != 1) {
                            entry.count = 1;
                            ++result.hostEntriesRecounted;
                        }
                    }
                    continue;
                }
                bool ownCalibers = false;
                for (std::size_t index = 0; index < count && !ownCalibers; ++index) {
                    const auto* form = list.leveledLists[index].form;
                    ownCalibers = !isAdded(form) && isCaliber(form);
                }
                if (ownCalibers) {
                    continue;
                }
                std::size_t kept = 0;
                for (std::size_t index = 0; index < count; ++index) {
                    if (!isAdded(list.leveledLists[index].form)) {
                        list.leveledLists[kept++] = list.leveledLists[index];
                    }
                }
                if (kept != count) {
                    result.hostEntriesRemoved += count - kept;
                    list.baseListCount = static_cast<std::int8_t>(kept);
                    spdlog::info("Host shape: {}|{:06X} rolls only other lists; {} entries of ours and the providers' removed", row.plugin, row.local, count - kept);
                }
            }
            spdlog::info("Host shape: {} entries set to one pick, {} removed", result.hostEntriesRecounted, result.hostEntriesRemoved);
        }
    }

    void warnIfIncompatible()
    {
        if (!s_incompatibleLoaded) {
            return;
        }
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            spdlog::warn("Caliber Complex warning not shown: the Papyrus VM is unavailable");
            return;
        }
        const RE::BSFixedString script{ "Debug" };
        const RE::BSFixedString function{ "Notification" };
        const RE::BSFixedString message{ "Flying BRICKS: Caliber Complex is incompatible. Disable it." };
        const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{};
        if (!Papyrus::detail::DispatchStaticCall(vm, script, function, callback, message)) {
            spdlog::warn("Caliber Complex warning not shown: Papyrus rejected the notification");
        }
    }

    ApplyResult apply(const patch_files::Patches& patches)
    {
        ApplyResult result;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            spdlog::error("TESDataHandler is unavailable; no compatibility is applied");
            return result;
        }

        const Active active{
            isActive(*dataHandler, kProviders[0]),
            isActive(*dataHandler, kProviders[1]),
            isActive(*dataHandler, kProviders[2]),
            isActive(*dataHandler, kProviders[3]),
        };
        result.providers = static_cast<std::size_t>(active.munitions) + active.armament + active.bibfx + active.wof;
        spdlog::info(
            "Providers: Munitions {}, Armament {}, BIBFX {}, Weapons of Fate {}",
            active.munitions, active.armament, active.bibfx, active.wof);
        s_incompatibleLoaded = plugin_forms::findLoaded(*dataHandler, kCaliberComplexPlugin) != nullptr;
        if (s_incompatibleLoaded) {
            spdlog::warn("Caliber Complex is incompatible and should be disabled: its rounds are not replaced by ours, so guns patched to them use their own damage, and its vendor and loot lists fill with rounds Flying_BRICKS does not use");
        }

        result.models = applyVisuals(*dataHandler, active);
        const Replacements replacements;
        if (!replacements.empty()) {
            applyReplacements(*dataHandler, replacements, result);
        }
        applyWeaponPatches(patches, result);
        applyWorldAmmo(*dataHandler, result);
        const auto rounds = caliberRounds(*dataHandler);
        applyLootScale(*dataHandler, rounds, result);
        applyHostShape(*dataHandler, rounds, result);
        spdlog::info("Weapon patches: {} weapons, {} object mods set, {} zeroed guns given their damage back, {} guns given another default part", result.patchedWeapons, result.patchedObjectMods, result.restoredDamage, result.swappedDefaults);
        return result;
    }
}
