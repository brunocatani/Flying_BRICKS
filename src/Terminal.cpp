#include "PCH.h"

#include "Terminal.h"

#include <bit>
#include <cmath>
#include <random>
#include <string>

namespace flying_bricks::terminal
{
    namespace
    {
        struct CaliberRow
        {
            std::uint32_t normalProjectile;
            float energyFtLb;
            std::uint8_t stopLevel;
            std::uint16_t headBurstRange;
            bool severs;
            float realWorldSpeed;
            float dragFactor;
        };

        struct ArmorRow
        {
            std::string_view plugin;
            std::uint32_t local;
            std::uint8_t level;
            std::uint8_t heavyLevel;
        };

        struct HeavyModRow
        {
            std::string_view plugin;
            std::uint32_t local;
        };

        // Fallout4.esm records: the mod's own level replaces its rating, which runs far above the armor it joins.
        struct WeaveModRow
        {
            std::uint32_t local;
            std::uint8_t level;
        };

        struct CreatureZone
        {
            std::uint8_t limb;
            std::uint8_t level;
            float factor;
            bool head;
        };

#include "TerminalData.inl"

        // The biped array holds one entry per body slot, game slot number minus 30.
        constexpr std::uint32_t kHeadSlot = 0;
        constexpr std::uint32_t kBodySlot = 3;
        constexpr std::array<std::uint32_t, 1> kHeadSlots{ kHeadSlot };
        constexpr std::array<std::uint32_t, 3> kTorsoSlots{ kBodySlot, 6, 11 };
        constexpr std::array<std::uint32_t, 3> kLeftArmSlots{ kBodySlot, 7, 12 };
        constexpr std::array<std::uint32_t, 3> kRightArmSlots{ kBodySlot, 8, 13 };
        constexpr std::array<std::uint32_t, 3> kLeftLegSlots{ kBodySlot, 9, 14 };
        constexpr std::array<std::uint32_t, 3> kRightLegSlots{ kBodySlot, 10, 15 };

        // Slots that make a single record a whole outfit rather than one piece.
        constexpr std::uint32_t kOutfitSlotMask = (1u << kBodySlot) | (1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15);
        constexpr int kOutfitSlotCount = 3;

        // Ratios of the vanilla combat set, which turn a piece's rating into torso terms.
        constexpr float kArmRatingScale = 1.7F;
        constexpr float kLegRatingScale = 1.9F;
        constexpr float kHelmetRatingScale = 1.5F;
        constexpr float kOutfitRatingScale = 0.3F;
        constexpr float kMinimumProtectiveRating = 5.0F;  // Below this, in torso terms, an item is clothing.
        constexpr std::uint8_t kHelmetLevelCap = 2;       // Real helmets are not built to stop rifle rounds.

        // Tuning values: no source converts these to health.
        constexpr float kRifleThroughSoftRetained = 0.9F;
        constexpr float kMinimumRetained = 0.25F;
        constexpr float kEnergyExponent = 0.6F;  // The damage model's own exponent.
        constexpr float kBehindSoftArmor = 0.10F;
        constexpr float kBehindHardArmor = 0.05F;
        constexpr float kBehindHelmet = 0.25F;
        constexpr float kArmFactor = 0.5F;
        constexpr float kLegFactor = 0.6F;
        constexpr float kHeadFactor = 2.0F;  // Targets without the instant kill, and the player with it off.
        constexpr std::uint8_t kFirstHardLevel = 3;

        // Power armor tuning, in energy so every caliber in the table falls somewhere without a list.
        // Lore gives the shape (small arms do nothing, anti-materiel rifles beat it), not these numbers.
        constexpr float kPlatePenetrationEnergy = 11000.0F;  // .50 BMG, 12.7x108, 13.2 TuF and heavier.
        constexpr float kPlateNoTransferEnergy = 1090.0F;    // The worn scale's top handgun level.
        constexpr float kPlateTransferPerFtLb = 1.0F / 120000.0F;  // 5.56 NATO passes 1% of its damage.
        constexpr float kPlateEnergyPerRating = 30.0F;  // What a piece's rating takes off a penetrating round.
        constexpr float kWearReferenceEnergy = 1196.0F;  // 5.56 NATO wears one point per hit.

        // Keeps the native write from losing a point: it truncates the new health to a whole number, and
        // health rebuilt from a stored percentage can land just under the whole number it was.
        constexpr float kPieceWriteMargin = 0.01F;

        // BGSBodyPart flags, read from raw disassembly of the removal decision in Fallout4VR.exe 1.2.72.0.
        constexpr std::uint8_t kPartCanSever = 1u << 0;
        constexpr std::uint8_t kPartCanBurst = 1u << 3;

        // Half of a 0.75 inch per yard pattern, between the 0.5 of modern stock barrels and the older
        // 1 inch rule of thumb.
        constexpr float kShotgunConeRadians = 0.0104F;

        // Fallout4.esm races. Gen 3 synths use HumanRace.
        constexpr std::array<std::uint32_t, 5> kInstantKillRaces{ 0x013746, 0x0EAFB6, 0x06B4EC, 0x0A96BF, 0x11EB96 };
        constexpr std::uint32_t kSuperMutantRace = 0x01A009;

        enum class Limb : std::uint8_t
        {
            Torso,
            Head,
            LeftArm,
            RightArm,
            LeftLeg,
            RightLeg,
        };

        struct ResolvedArmor
        {
            const RE::TESObjectARMO* form;
            std::uint8_t level;
            std::uint8_t heavyLevel;
        };

        std::vector<ResolvedArmor> s_armor;  // Sorted by form.
        std::vector<std::uint32_t> s_heavyMods;  // Sorted runtime form IDs.
        std::size_t s_weaveMods = 0;             // Rows found at load, reported in the build result.

        struct ResolvedCreature
        {
            const RE::TESRace* race;
            const CreatureRow* row;
        };
        std::vector<ResolvedCreature> s_creatures;  // Sorted by race.
        std::array<const RE::TESRace*, kInstantKillRaces.size()> s_instantKillRaces{};
        const RE::TESRace* s_superMutantRace = nullptr;
        std::array<Caliber, kCaliberRows.size()> s_calibers{};

        std::atomic_bool s_playerInstantKill{ true };
        std::atomic_bool s_loggedWornFault{ false };

        // ActorInPowerArmor, read from raw disassembly of Fallout4VR.exe 1.2.72.0.
        constexpr std::uintptr_t kActorInPowerArmor = 0x009BF5D0;
        using ActorInPowerArmorFunction = bool (*)(RE::Actor*);

        [[nodiscard]] std::string lowered(std::string_view text)
        {
            std::string result(text);
            std::ranges::transform(result, result.begin(), [](char value) {
                return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
            });
            return result;
        }

        // The first source file is the record's own plugin, whatever later files override it.
        [[nodiscard]] std::string masterOf(const RE::TESForm& form)
        {
            const auto* files = form.sourceFiles.array;
            return files && !files->empty() && files->front() ? lowered(files->front()->GetFilename()) : std::string{};
        }

        [[nodiscard]] Limb classifyLimb(std::uint32_t limb) noexcept
        {
            // BGSBodyPartDefs::LIMB_ENUM. Head parts: head, eye, second head part, brain.
            switch (limb) {
            case 1:
            case 2:
            case 5:
            case 16:
                return Limb::Head;
            case 6:
            case 7:
                return Limb::LeftArm;
            case 8:
            case 9:
            case 17:  // The weapon limb, which HumanBodyPartData puts on the right hand.
                return Limb::RightArm;
            case 10:
            case 11:
            case 12:
            case 23:
                return Limb::LeftLeg;
            case 13:
            case 14:
            case 15:
            case 24:
                return Limb::RightLeg;
            default:
                return Limb::Torso;
            }
        }

        [[nodiscard]] std::span<const std::uint32_t> slotsOf(Limb limb) noexcept
        {
            switch (limb) {
            case Limb::Head:
                return kHeadSlots;
            case Limb::LeftArm:
                return kLeftArmSlots;
            case Limb::RightArm:
                return kRightArmSlots;
            case Limb::LeftLeg:
                return kLeftLegSlots;
            case Limb::RightLeg:
                return kRightLegSlots;
            default:
                return kTorsoSlots;
            }
        }

        [[nodiscard]] std::uint8_t levelFromRating(float rating, std::uint32_t slotMask) noexcept
        {
            const bool outfit = std::popcount(slotMask & kOutfitSlotMask) >= kOutfitSlotCount;
            float scale = 1.0F;
            if (outfit) {
                scale = kOutfitRatingScale;
            } else if ((slotMask & (1u << kHeadSlot)) != 0) {
                scale = kHelmetRatingScale;
            } else if ((slotMask & ((1u << 12) | (1u << 13))) != 0) {
                scale = kArmRatingScale;
            } else if ((slotMask & ((1u << 14) | (1u << 15))) != 0) {
                scale = kLegRatingScale;
            }
            const float scaled = rating * scale;
            if (scaled < kMinimumProtectiveRating) {
                return 0;
            }
            return scaled < 15.0F ? 1 : scaled < 25.0F ? 2 : scaled < 40.0F ? 3 : 4;
        }

        // Not noexcept, like levelOfItem: both run inside readWornLevel's structured exception guard.
        [[nodiscard]] bool hasHeavyMod(const RE::BGSObjectInstanceExtra* mods)
        {
            if (!mods || !mods->values) {
                return false;
            }
            for (const auto& mod : mods->GetIndexData()) {
                if (std::ranges::binary_search(s_heavyMods, mod.objectID)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::uint8_t weaveLevelOf(const RE::BGSObjectInstanceExtra* mods)
        {
            if (!mods || !mods->values) {
                return 0;
            }
            std::uint8_t highest = 0;
            for (const auto& mod : mods->GetIndexData()) {
                // Fallout4.esm's load index is always zero, so the local ID is the runtime ID.
                const auto row = std::ranges::lower_bound(kWeaveModRows, mod.objectID, {}, &WeaveModRow::local);
                if (row != kWeaveModRows.end() && row->local == mod.objectID) {
                    highest = std::max(highest, row->level);
                }
            }
            return highest;
        }

        [[nodiscard]] std::uint8_t levelOfItem(const RE::BIPOBJECT& worn, bool head)
        {
            const auto* item = worn.parent.object;
            const auto* armor = item ? item->As<RE::TESObjectARMO>() : nullptr;
            if (!armor) {
                return 0;
            }
            const auto weave = weaveLevelOf(worn.modExtra);
            // Weave plates the body: an outfit that includes a hat is still not a helmet.
            const std::uint8_t plating = head ? 0 : weave;
            const auto row = std::ranges::lower_bound(s_armor, armor, {}, &ResolvedArmor::form);
            if (row != s_armor.end() && row->form == armor) {
                return std::max(plating, hasHeavyMod(worn.modExtra) ? row->heavyLevel : row->level);
            }
            const auto* instance = static_cast<const RE::TESObjectARMO::InstanceData*>(worn.parent.instanceData.get());
            const auto rating = instance && weave == 0 ? instance->rating : armor->armorData.rating;
            return std::max(plating, levelFromRating(static_cast<float>(rating), armor->GetSlotMask()));
        }

        // Guarded because hits run on worker threads while the main thread may rebuild the biped data
        // when an actor changes clothes. A fault counts as no armor on that limb and is logged once.
        [[nodiscard]] int readWornLevel(const RE::Actor& target, std::span<const std::uint32_t> slots, bool head) noexcept
        {
            __try {
                const auto* biped = target.biped.get();
                if (!biped) {
                    return -1;
                }
                std::uint8_t highest = 0;
                for (const auto slot : slots) {
                    highest = std::max(highest, levelOfItem(biped->object[slot], head));
                }
                return highest;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return -2;
            }
        }

        [[nodiscard]] std::uint8_t wornLevel(const RE::Actor& target, Limb limb) noexcept
        {
            const int level = readWornLevel(target, slotsOf(limb), limb == Limb::Head);
            if (level >= 0) {
                const auto worn = static_cast<std::uint8_t>(level);
                return limb == Limb::Head ? std::min(worn, kHelmetLevelCap) : worn;
            }
            if (level == -2 && !s_loggedWornFault.exchange(true, std::memory_order_relaxed)) {
                spdlog::error("Reading worn armor on {:08X} faulted; the limb counts as unarmored", target.GetFormID());
            }
            return 0;
        }

        [[nodiscard]] float retainedAfter(const Caliber& caliber, std::uint8_t armorLevel) noexcept
        {
            if (armorLevel == 0) {
                return 1.0F;
            }
            if (caliber.stopLevel >= kFirstHardLevel && armorLevel < kFirstHardLevel) {
                return kRifleThroughSoftRetained;
            }
            const float energy = caliber.energyFtLb * caliber.energyKept;
            const float left = energy > 0.0F ? (energy - kRatedEnergy[armorLevel]) / energy : 0.0F;
            return left <= 0.0F ? kMinimumRetained : std::max(kMinimumRetained, std::pow(left, kEnergyExponent));
        }

        // A creature's body read as armor: its zone's level meets the caliber's stop level the same way
        // worn armor does, so the stop table and retained energy need no second set of numbers.
        [[nodiscard]] Outcome resolveCreature(const CreatureRow& row, const Caliber& caliber, std::uint32_t limb) noexcept
        {
            Outcome outcome;
            outcome.active = true;
            outcome.armorLevel = row.level;
            outcome.vanillaRemoval = row.vanillaRemoval;
            for (std::size_t index = 0; index < row.zoneCount; ++index) {
                const auto& zone = row.zones[index];
                if (zone.limb == limb) {
                    outcome.armorLevel = zone.level;
                    outcome.limbFactor = zone.factor;
                    outcome.head = zone.head;
                    break;
                }
            }
            outcome.stopped = outcome.armorLevel >= caliber.stopLevel;
            if (outcome.stopped) {
                outcome.armorFraction = outcome.armorLevel < kFirstHardLevel ? kBehindSoftArmor : kBehindHardArmor;
                return outcome;
            }
            outcome.armorFraction = retainedAfter(caliber, outcome.armorLevel);
            outcome.instantKill = outcome.head && row.headKill;
            return outcome;
        }

        [[nodiscard]] float limbFactorOf(Limb limb) noexcept
        {
            switch (limb) {
            case Limb::LeftArm:
            case Limb::RightArm:
                return kArmFactor;
            case Limb::LeftLeg:
            case Limb::RightLeg:
                return kLegFactor;
            default:
                return 1.0F;
            }
        }

        // The biped entry holding the suit piece for a limb. The frame's own skin shares some of these
        // entries with no rating, so a rating is what tells a piece from the bare frame.
        [[nodiscard]] std::uint32_t plateSlotOf(Limb limb) noexcept
        {
            switch (limb) {
            case Limb::Head:
                return kHeadSlot;
            case Limb::LeftArm:
                return 12;
            case Limb::RightArm:
                return 13;
            case Limb::LeftLeg:
                return 14;
            case Limb::RightLeg:
                return 15;
            default:
                return 11;
            }
        }

        // Not noexcept, like levelOfItem: it runs inside readPlateRating's structured exception guard.
        [[nodiscard]] int ratingOfItem(const RE::BIPOBJECT& worn)
        {
            const auto* item = worn.parent.object;
            const auto* armor = item ? item->As<RE::TESObjectARMO>() : nullptr;
            if (!armor) {
                return 0;
            }
            const auto* instance = static_cast<const RE::TESObjectARMO::InstanceData*>(worn.parent.instanceData.get());
            return instance ? instance->rating : armor->armorData.rating;
        }

        // Guarded for the same reason as readWornLevel.
        [[nodiscard]] int readPlateRating(const RE::Actor& target, std::uint32_t slot) noexcept
        {
            __try {
                const auto* biped = target.biped.get();
                return biped ? ratingOfItem(biped->object[slot]) : -1;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return -2;
            }
        }

        // A round against an intact piece. Stopped rounds never kill or remove anything.
        [[nodiscard]] Outcome resolvePlate(const Caliber& caliber, Limb hitLimb, int rating, std::uint32_t pellets, bool headKills) noexcept
        {
            // The class at the muzzle decides whether a round crosses the piece; the energy it arrives with
            // decides how much of it does.
            const float muzzle = caliber.energyFtLb / static_cast<float>(std::max(pellets, 1u));
            const float energy = muzzle * caliber.energyKept;
            const bool head = hitLimb == Limb::Head;
            Outcome outcome;
            outcome.active = true;
            outcome.armorLevel = kPlateLevel;
            outcome.head = head;
            outcome.stopped = muzzle < kPlatePenetrationEnergy;
            if (outcome.stopped) {
                outcome.armorFraction = muzzle <= kPlateNoTransferEnergy ? 0.0F : energy * kPlateTransferPerFtLb;
                outcome.limbFactor = head ? 1.0F : limbFactorOf(hitLimb);
                return outcome;
            }
            const float left = energy > 0.0F ? (energy - static_cast<float>(rating) * kPlateEnergyPerRating) / energy : 0.0F;
            outcome.armorFraction = left <= 0.0F ? kMinimumRetained : std::max(kMinimumRetained, std::pow(left, kEnergyExponent));
            outcome.limbFactor = head ? kHeadFactor : limbFactorOf(hitLimb);
            outcome.instantKill = head && headKills;
            return outcome;
        }
    }

    BuildResult build()
    {
        BuildResult result;
        for (std::size_t index = 0; index < kCaliberRows.size(); ++index) {
            const auto& row = kCaliberRows[index];
            s_calibers[index] = Caliber{ row.energyFtLb, row.stopLevel, row.headBurstRange, row.severs, row.realWorldSpeed, row.dragFactor };
        }
        for (std::size_t index = 0; index < kInstantKillRaces.size(); ++index) {
            s_instantKillRaces[index] = RE::TESForm::GetFormByID<RE::TESRace>(kInstantKillRaces[index]);
            if (!s_instantKillRaces[index]) {
                spdlog::error("Race {:08X} was not found", kInstantKillRaces[index]);
            }
        }
        s_superMutantRace = RE::TESForm::GetFormByID<RE::TESRace>(kSuperMutantRace);

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            spdlog::error("TESDataHandler is unavailable; every armor counts by its rating");
            return result;
        }

        for (auto* race : dataHandler->GetFormArray<RE::TESRace>()) {
            if (!race) {
                continue;
            }
            const auto master = masterOf(*race);
            const auto local = race->GetFormID() & 0xFFFFFF;
            const auto row = std::ranges::find_if(kCreatureRows, [&](const CreatureRow& candidate) {
                return candidate.local == local && candidate.plugin == master;
            });
            if (row != kCreatureRows.end()) {
                s_creatures.push_back(ResolvedCreature{ race, &*row });
            }
        }
        std::ranges::sort(s_creatures, {}, &ResolvedCreature::race);
        result.creatures = s_creatures.size();

        for (auto* armor : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
            if (!armor) {
                continue;
            }
            const auto master = masterOf(*armor);
            const auto local = armor->GetFormID() & 0xFFFFFF;
            const auto row = std::ranges::find_if(kArmorRows, [&](const ArmorRow& candidate) {
                return candidate.local == local && candidate.plugin == master;
            });
            if (row != kArmorRows.end()) {
                s_armor.push_back(ResolvedArmor{ armor, row->level, row->heavyLevel });
            } else if (armor->armorData.rating > 0) {
                ++result.armorByRating;
                spdlog::info(
                    "Armor {:08X} from {} has no template; level by rating {} is {}",
                    armor->GetFormID(),
                    master,
                    armor->armorData.rating,
                    levelFromRating(static_cast<float>(armor->armorData.rating), armor->GetSlotMask()));
            }
        }
        std::ranges::sort(s_armor, {}, &ResolvedArmor::form);

        for (auto* mod : dataHandler->GetFormArray<RE::BGSMod::Attachment::Mod>()) {
            if (!mod) {
                continue;
            }
            const auto master = masterOf(*mod);
            const auto local = mod->GetFormID() & 0xFFFFFF;
            if (std::ranges::any_of(kHeavyModRows, [&](const HeavyModRow& row) { return row.local == local && row.plugin == master; })) {
                s_heavyMods.push_back(mod->GetFormID());
            }
            if (std::ranges::any_of(kWeaveModRows, [&](const WeaveModRow& row) { return row.local == mod->GetFormID(); })) {
                ++s_weaveMods;
            }
        }
        std::ranges::sort(s_heavyMods);

        result.armorRows = s_armor.size();
        result.heavyMods = s_heavyMods.size();
        result.weaveMods = s_weaveMods;
        return result;
    }

    const Caliber* findCaliber(const RE::BGSProjectile& normalProjectile) noexcept
    {
        const auto local = normalProjectile.GetFormID() & 0xFFFFFF;
        const auto row = std::ranges::lower_bound(kCaliberRows, local, {}, &CaliberRow::normalProjectile);
        if (row == kCaliberRows.end() || row->normalProjectile != local) {
            return nullptr;
        }
        return &s_calibers[static_cast<std::size_t>(row - kCaliberRows.begin())];
    }

    void setPlayerHeadshotInstantKill(bool enabled) noexcept
    {
        s_playerInstantKill.store(enabled, std::memory_order_relaxed);
    }

    Outcome resolve(RE::Actor& target, const Caliber& caliber, std::uint32_t limb, std::uint32_t pellets) noexcept
    {
        const auto* race = target.race;
        if (!race) {
            return {};
        }
        const bool instantKillRace = std::ranges::find(s_instantKillRaces, race) != s_instantKillRaces.end();
        if (!instantKillRace && race != s_superMutantRace) {
            const auto creature = std::ranges::lower_bound(s_creatures, race, {}, &ResolvedCreature::race);
            if (creature != s_creatures.end() && creature->race == race) {
                return resolveCreature(*creature->row, caliber, limb);
            }
        }
        // Wearing the suit changes the actor's race, so this comes before any test of who the actor is.
        // The native test locks the inventory list without checking that one exists.
        static const REL::Relocation<ActorInPowerArmorFunction> s_actorInPowerArmor{ REL::Offset(kActorInPowerArmor) };
        const bool inPowerArmor = target.inventoryList && s_actorInPowerArmor(&target);
        if (!inPowerArmor && !instantKillRace && race != s_superMutantRace) {
            return {};
        }

        const auto hitLimb = classifyLimb(limb);
        const bool isPlayer = &target == RE::PlayerCharacter::GetSingleton();
        // Only people fit in the frame, and the suit hides their own race.
        const bool headKills = (instantKillRace || inPowerArmor) && (!isPlayer || s_playerInstantKill.load(std::memory_order_relaxed));

        Outcome outcome;
        outcome.active = true;
        if (inPowerArmor) {
            const int rating = readPlateRating(target, plateSlotOf(hitLimb));
            if (rating == -2 && !s_loggedWornFault.exchange(true, std::memory_order_relaxed)) {
                spdlog::error("Reading the suit piece on {:08X} faulted; the limb counts as unarmored", target.GetFormID());
            }
            if (rating > 0) {
                return resolvePlate(caliber, hitLimb, rating, pellets, headKills);
            }
        } else {
            outcome.armorLevel = wornLevel(target, hitLimb);
        }
        outcome.stopped = outcome.armorLevel >= caliber.stopLevel;
        const bool head = hitLimb == Limb::Head;
        outcome.head = head;

        if (outcome.stopped) {
            outcome.armorFraction = head ? kBehindHelmet : outcome.armorLevel < kFirstHardLevel ? kBehindSoftArmor : kBehindHardArmor;
            outcome.limbFactor = head ? 1.0F : limbFactorOf(hitLimb);
            return outcome;
        }

        outcome.armorFraction = retainedAfter(caliber, outcome.armorLevel);
        if (!head) {
            outcome.limbFactor = limbFactorOf(hitLimb);
            return outcome;
        }
        outcome.instantKill = headKills;
        outcome.limbFactor = kHeadFactor;
        return outcome;
    }

    float pieceDamage(const Caliber& caliber, std::uint32_t pellets) noexcept
    {
        // Native health moves in whole points, so a fraction of a point becomes a chance of one. Squared
        // because a light round barely marks a plate that a heavy one tears: 10mm 0.32 points, 5.56 1,
        // .308 6.3, .338 16, .50 BMG 122, against 50 to 560 health on a suit piece.
        const float energy = caliber.energyFtLb * caliber.energyKept / static_cast<float>(std::max(pellets, 1u));
        const float ratio = energy / kWearReferenceEnergy;
        return pieceDamage(ratio * ratio);
    }

    float pieceDamage(float wear) noexcept
    {
        thread_local std::minstd_rand s_random{ static_cast<std::uint32_t>(GetTickCount64()) ^ GetCurrentThreadId() };
        const float roll = static_cast<float>(s_random() - std::minstd_rand::min()) / static_cast<float>(std::minstd_rand::max() - std::minstd_rand::min());
        const float whole = std::floor(wear);
        return whole + (roll < wear - whole ? 1.0F : 0.0F) - kPieceWriteMargin;
    }

    Outcome resolveMelee(RE::Actor& target, const Protection& protection, std::uint32_t limb) noexcept
    {
        const auto* race = target.race;
        if (!race) {
            return {};
        }
        Outcome outcome;
        outcome.active = true;
        const bool person = race == s_superMutantRace || std::ranges::find(s_instantKillRaces, race) != s_instantKillRaces.end();
        if (!person) {
            const auto creature = std::ranges::lower_bound(s_creatures, race, {}, &ResolvedCreature::race);
            if (creature != s_creatures.end() && creature->race == race) {
                const auto& row = *creature->row;
                outcome.armorLevel = row.level;
                for (std::size_t index = 0; index < row.zoneCount; ++index) {
                    const auto& zone = row.zones[index];
                    if (zone.limb == limb) {
                        outcome.armorLevel = zone.level;
                        outcome.limbFactor = zone.factor;
                        outcome.head = zone.head;
                        break;
                    }
                }
                outcome.armorFraction = protection.worn[std::min<std::size_t>(outcome.armorLevel, protection.worn.size() - 1)];
                outcome.stopped = outcome.armorLevel >= kFirstHardLevel;
                outcome.vanillaRemoval = row.vanillaRemoval;
                return outcome;
            }
        }
        static const REL::Relocation<ActorInPowerArmorFunction> s_actorInPowerArmor{ REL::Offset(kActorInPowerArmor) };
        const bool inPowerArmor = target.inventoryList && s_actorInPowerArmor(&target);
        if (!inPowerArmor && !person) {
            return {};
        }

        const auto hitLimb = classifyLimb(limb);
        const bool head = hitLimb == Limb::Head;
        outcome.head = head;
        if (inPowerArmor && readPlateRating(target, plateSlotOf(hitLimb)) > 0) {
            outcome.armorLevel = kPlateLevel;
            outcome.stopped = true;
            outcome.armorFraction = protection.plate;
            outcome.limbFactor = head ? 1.0F : limbFactorOf(hitLimb);
            return outcome;
        }
        outcome.armorLevel = inPowerArmor ? std::uint8_t{ 0 } : wornLevel(target, hitLimb);
        outcome.armorFraction = protection.worn[std::min<std::size_t>(outcome.armorLevel, protection.worn.size() - 1)];
        outcome.stopped = outcome.armorLevel >= kFirstHardLevel;
        // A helmet spreads the blow even when it does not stop it.
        outcome.limbFactor = head ? (outcome.armorLevel == 0 ? kHeadFactor : 1.0F) : limbFactorOf(hitLimb);
        return outcome;
    }

    Removal removal(const Caliber& caliber, const Outcome& outcome, std::uint32_t hitLimb, std::uint32_t partLimb, std::uint8_t partFlags, float distance) noexcept
    {
        if (!outcome.active || outcome.stopped || hitLimb != partLimb) {
            return {};
        }
        // The part decides how it comes off: a head that cannot burst (a synth's neck) is severed, and a
        // limb that cannot be severed (a robot's arm) is shot off. The native decision refuses anything else.
        const bool canSever = (partFlags & kPartCanSever) != 0;
        const bool canBurst = (partFlags & kPartCanBurst) != 0;
        if (outcome.head) {
            const bool inRange = caliber.headBurstRange == kAnyRange || (caliber.headBurstRange > 0 && distance <= static_cast<float>(caliber.headBurstRange));
            return inRange ? Removal{ canSever && !canBurst, canBurst } : Removal{};
        }
        const bool wanted = caliber.severs && classifyLimb(hitLimb) != Limb::Torso;
        return wanted ? Removal{ canSever, canBurst && !canSever } : Removal{};
    }

    float damageShare(const Caliber& impact) noexcept
    {
        return impact.energyKept >= 1.0F ? 1.0F : std::pow(std::max(impact.energyKept, 0.0F), kEnergyExponent);
    }

    float shotgunConeRadians() noexcept
    {
        return kShotgunConeRadians;
    }
}
