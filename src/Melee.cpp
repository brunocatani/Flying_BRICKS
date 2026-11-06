#include "PCH.h"

#include "Melee.h"

namespace flying_bricks::melee
{
    namespace
    {
        // Share of a blow that gets through, by worn armor level 0 to 4, then behind an intact power armor piece.
        // Tuning values: no source converts these to health. The shape is what matters: an edge is defeated by
        // anything hard and goes through cloth, while force crosses plate and only a heavy tool or a suit's fist
        // reaches a man inside a suit.
        constexpr terminal::Protection kPoint{ { 1.0F, 0.6F, 0.3F, 0.08F, 0.05F }, 0.0F };
        constexpr terminal::Protection kEdge{ { 1.0F, 0.45F, 0.2F, 0.08F, 0.05F }, 0.0F };
        constexpr terminal::Protection kHeavyEdge{ { 1.0F, 0.55F, 0.3F, 0.12F, 0.08F }, 0.01F };
        constexpr terminal::Protection kSaw{ { 1.0F, 0.6F, 0.35F, 0.1F, 0.06F }, 0.01F };
        constexpr terminal::Protection kFist{ { 1.0F, 0.7F, 0.5F, 0.25F, 0.15F }, 0.0F };
        constexpr terminal::Protection kBlunt{ { 1.0F, 0.85F, 0.65F, 0.4F, 0.3F }, 0.01F };
        // Through a suit, a suit's punch lands about where a 5.56 round does.
        constexpr terminal::Protection kCrush{ { 1.0F, 0.9F, 0.75F, 0.55F, 0.45F }, 0.02F };
        constexpr terminal::Protection kClaw{ { 1.0F, 0.7F, 0.5F, 0.2F, 0.12F }, 0.03F };

        constexpr float kWearNone = 0.0F;
        constexpr float kWearPoint = 0.002F;
        constexpr float kWearEdge = 0.005F;
        constexpr float kWearHeavy = 0.02F;
        constexpr float kWearCrush = 0.06F;
        constexpr float kWearClaw = 0.04F;

        constexpr Blow kFists{ 12.0F, &kFist, kWearNone };
        constexpr Blow kBoxingGlove{ 8.0F, &kFist, kWearNone };
        constexpr Blow kKnuckles{ 18.0F, &kFist, kWearNone };
        constexpr Blow kPowerFist{ 45.0F, &kCrush, kWearCrush };
        constexpr Blow kGauntlet{ 55.0F, &kClaw, kWearClaw };
        constexpr Blow kSuitFist{ 40.0F, &kCrush, kWearCrush };
        constexpr Blow kSwitchblade{ 30.0F, &kPoint, kWearPoint };
        constexpr Blow kKnife{ 35.0F, &kPoint, kWearPoint };
        constexpr Blow kMachete{ 50.0F, &kEdge, kWearEdge };
        constexpr Blow kSword{ 60.0F, &kEdge, kWearEdge };
        constexpr Blow kShishkebab{ 45.0F, &kEdge, kWearEdge };
        constexpr Blow kRipper{ 12.0F, &kSaw, kWearHeavy };
        constexpr Blow kLightBlunt{ 20.0F, &kFist, kWearEdge };
        constexpr Blow kClub{ 35.0F, &kBlunt, kWearHeavy };
        constexpr Blow kGunBash{ 25.0F, &kBlunt, kWearHeavy };
        constexpr Blow kSledge{ 70.0F, &kCrush, kWearCrush };
        constexpr Blow kSuperSledge{ 100.0F, &kCrush, kWearCrush };

        // The game's own number, for what has no entry above.
        constexpr Blow kNaturalPoint{ 0.0F, &kPoint, kWearPoint };
        constexpr Blow kNaturalEdge{ 0.0F, &kEdge, kWearEdge };
        constexpr Blow kNaturalAxe{ 0.0F, &kHeavyEdge, kWearHeavy };
        constexpr Blow kNaturalFist{ 0.0F, &kFist, kWearNone };
        constexpr Blow kNaturalBlunt{ 0.0F, &kBlunt, kWearHeavy };
        constexpr Blow kNaturalCrush{ 0.0F, &kCrush, kWearCrush };
        constexpr Blow kNaturalClaw{ 0.0F, &kClaw, kWearClaw };

        struct KeywordRow
        {
            std::uint32_t form;
            const Blow* blow;
        };

        // Fallout4.esm mod-association keywords, which survive mods and leveled copies. An animation type alone
        // misleads: the Super Sledge is a two-handed "sword".
        constexpr std::array<KeywordRow, 26> kKeywordRows{ {
            { 0x187A0B, &kKnife },
            { 0x187846, &kSwitchblade },
            { 0x187850, &kMachete },
            { 0x18782C, &kSword },
            { 0x18783D, &kSword },
            { 0x14EC5B, &kShishkebab },
            { 0x225768, &kShishkebab },
            { 0x188A8F, &kRipper },
            { 0x225767, &kRipper },
            { 0x0B1D2C, &kRipper },
            { 0x0B1D2E, &kNaturalCrush },
            { 0x187A21, &kBoxingGlove },
            { 0x0E7FB4, &kKnuckles },
            { 0x185D00, &kPowerFist },
            { 0x187A07, &kGauntlet },
            { 0x185D1B, &kLightBlunt },
            { 0x185D12, &kLightBlunt },
            { 0x185D0F, &kLightBlunt },
            { 0x18782A, &kLightBlunt },
            { 0x184A2D, &kClub },
            { 0x187814, &kClub },
            { 0x187820, &kClub },
            { 0x185D24, &kClub },
            { 0x0E7FB3, &kClub },
            { 0x184A2F, &kSledge },
            { 0x0690B0, &kSuperSledge },
        } };

        struct ResolvedKeyword
        {
            const RE::BGSKeyword* keyword;
            const Blow* blow;
        };
        std::array<ResolvedKeyword, kKeywordRows.size()> s_keywords{};

        // Fallout4.esm unarmed weapons of people, creatures and robots.
        [[nodiscard]] const Blow* unarmedBlow(std::uint32_t form) noexcept
        {
            switch (form) {
            case 0x0C2C27:  // Human.
            case 0x0001F4:  // Default.
            case 0x23F492:  // Synth.
                return &kFists;
            case 0x0C2C32:  // Power armor.
                return &kSuitFist;
            case 0x0C2C39:  // Vicious dog.
            case 0x0C2C2B:  // Dogmeat.
            case 0x0C2C2D:  // Mutant hound.
            case 0x0C2C31:  // Mole rat.
            case 0x0C2C34:  // Radroach.
            case 0x0C2C35:  // Radscorpion.
            case 0x0C2C36:  // Stingwing.
            case 0x0C9799:  // Stingwing, no poison.
            case 0x0C2C28:  // Bloodbug.
            case 0x0C2C33:  // Radstag.
                return &kNaturalPoint;
            case 0x0C2C2C:  // Feral ghoul.
            case 0x1C403B:  // Glowing one.
            case 0x0DF25A:  // Assaultron claw.
            case 0x0DF259:  // Assaultron claw.
            case 0x210B62:  // Assaultron blade.
            case 0x210B63:  // Assaultron blade.
                return &kNaturalEdge;
            case 0x0C2C2A:  // Deathclaw.
            case 0x0C2C38:  // Yao guai.
                return &kNaturalClaw;
            case 0x0C2C30:  // Mirelurk.
            case 0x0C2C2E:  // Mirelurk hunter.
            case 0x0C2C2F:  // Mirelurk king.
            case 0x102796:  // Mirelurk queen.
            case 0x0C2C29:  // Brahmin.
            case 0x0EB340:  // Super mutant.
            case 0x0ED6E4:  // Gorilla.
            case 0x1BD15B:  // Protectron.
            case 0x1BD15A:  // Assaultron.
            case 0x0C2C37:  // Behemoth.
            case 0x1BDDB7:  // Liberty Prime.
            case 0x10FFFD:  // Sentry bot.
                return &kNaturalCrush;
            default:
                return nullptr;
            }
        }
    }

    void build()
    {
        for (std::size_t index = 0; index < kKeywordRows.size(); ++index) {
            // Fallout4.esm's load index is always zero, so the local ID is the runtime ID.
            s_keywords[index] = ResolvedKeyword{ RE::TESForm::GetFormByID<RE::BGSKeyword>(kKeywordRows[index].form), kKeywordRows[index].blow };
            if (!s_keywords[index].keyword) {
                spdlog::error("Melee keyword {:08X} was not found; weapons with it fall back to their animation type", kKeywordRows[index].form);
            }
        }
    }

    const Blow& classify(const RE::TESObjectWEAP& weapon, const RE::TBO_InstanceData* instance, bool bash) noexcept
    {
        if (bash) {
            return kGunBash;
        }
        if (const auto* blow = unarmedBlow(weapon.GetFormID())) {
            return *blow;
        }
        for (const auto& row : s_keywords) {
            if (row.keyword && weapon.HasKeyword(row.keyword, instance)) {
                return *row.blow;
            }
        }
        using Type = RE::WEAPON_TYPE;
        switch (weapon.weaponData.type.get()) {
        case Type::kHandToHand:
            return kNaturalFist;
        case Type::kOneHandDagger:
            return kNaturalPoint;
        case Type::kOneHandAxe:
        case Type::kTwoHandAxe:
            return kNaturalAxe;
        default:
            return kNaturalBlunt;  // An unknown "sword" animation is a club more often than a blade.
        }
    }

    float damageOf(const Blow& blow, const RE::TESObjectWEAP& weapon, const RE::TBO_InstanceData* instance) noexcept
    {
        constexpr float kLargestModRatio = 2.0F;
        const auto base = weapon.weaponData.attackDamage;
        if (blow.damage <= 0.0F || !instance || base == 0) {
            return blow.damage;
        }
        const auto* data = static_cast<const RE::TESObjectWEAP::InstanceData*>(instance);
        const float ratio = static_cast<float>(data->attackDamage) / static_cast<float>(base);
        return blow.damage * std::clamp(ratio, 1.0F, kLargestModRatio);
    }

    float pieceWear(const Blow& blow, const RE::TESObjectWEAP& weapon, const RE::TBO_InstanceData* instance, float physicalDamage) noexcept
    {
        const float share = blow.protection->plate;
        if (share > 0.0F) {
            return blow.wearPerDamage * std::max(physicalDamage, 0.0F) / share;
        }
        // Nothing crossed the plate, so the hit no longer holds the blow's size.
        const float authored = damageOf(blow, weapon, instance);
        return blow.wearPerDamage * (authored > 0.0F ? authored : static_cast<float>(weapon.weaponData.attackDamage));
    }
}
