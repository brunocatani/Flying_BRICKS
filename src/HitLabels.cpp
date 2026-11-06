#include "PCH.h"

#include "HitLabels.h"

#include <RPS_UI_Framework/Client.h>
#include <RPS_UI_Framework/Framework.h>
#include <RPS_UI_Framework/WorldDraw.h>

#include <cstdio>
#include <mutex>

namespace flying_bricks::hit_labels
{
    namespace
    {
        namespace draw = rpsui::api::world_draw;

        constexpr std::size_t kLabelCount = 16;
        constexpr std::uint64_t kLabelLifetimeMs = 5000;
        constexpr float kMarkerRadius = 1.5F;

        struct Label
        {
            float location[3]{};
            char text[32]{};
            bool authored{};
            std::uint64_t createdMs{};
        };

        const draw::Api* s_draw = nullptr;
        std::uint64_t s_owner = 0;  // Set before the frame callback is registered, then read-only.
        bool s_callbackRegistered = false;

        std::atomic_bool s_enabled{ false };
        std::mutex s_lock;
        std::array<Label, kLabelCount> s_labels{};  // Guarded by s_lock.
        std::size_t s_nextLabel = 0;                // Guarded by s_lock.

        bool s_published = false;  // Render thread only.

        void setColor(float (&color)[4], bool authored) noexcept
        {
            // Green for the authored model, amber for a native hit.
            color[0] = authored ? 0.2F : 1.0F;
            color[1] = authored ? 1.0F : 0.7F;
            color[2] = 0.2F;
            color[3] = 1.0F;
        }

        void RPSUI_CALL onFrame(void*, std::uint64_t) noexcept
        {
            if (!s_enabled.load(std::memory_order_relaxed)) {
                if (s_published) {
                    s_draw->clear(s_owner);
                    s_published = false;
                }
                return;
            }

            std::array<draw::Sphere, kLabelCount> spheres{};
            std::array<draw::Text, kLabelCount> texts{};
            std::uint32_t count = 0;
            {
                std::unique_lock lock(s_lock, std::try_to_lock);
                if (!lock.owns_lock()) {
                    return;  // The previous publication lasts one more frame's lease at most.
                }
                const auto now = GetTickCount64();
                for (const auto& label : s_labels) {
                    if (label.createdMs == 0 || now - label.createdMs > kLabelLifetimeMs) {
                        continue;
                    }
                    auto& sphere = spheres[count];
                    auto& text = texts[count];
                    std::memcpy(sphere.center, label.location, sizeof(sphere.center));
                    sphere.radius = kMarkerRadius;
                    setColor(sphere.color, label.authored);
                    std::memcpy(text.anchor, label.location, sizeof(text.anchor));
                    std::memcpy(text.text, label.text, sizeof(label.text));
                    setColor(text.color, label.authored);
                    ++count;
                }
            }

            if (count == 0) {
                if (s_published) {
                    s_draw->clear(s_owner);
                    s_published = false;
                }
                return;
            }
            draw::Publication publication;
            publication.leaseFrames = 2;
            publication.sphereCount = count;
            publication.spheres = spheres.data();
            publication.textCount = count;
            publication.texts = texts.data();
            s_published = s_draw->submit(s_owner, &publication) == rpsui::api::Result::Ok;
        }
    }

    void connect() noexcept
    {
        if (s_callbackRegistered) {
            return;
        }
        const auto module = GetModuleHandleW(rpsui::api::kModuleName);
        const auto query = module ? reinterpret_cast<rpsui::api::QueryInterfaceV1>(GetProcAddress(module, rpsui::api::kQueryExportName)) : nullptr;
        if (!query) {
            spdlog::info("RPS UI is not loaded; hit labels are unavailable");
            return;
        }
        const rpsui::api::framework::Api* framework = nullptr;
        const draw::Api* worldDraw = nullptr;
        if (rpsui::api::acquire(query, framework) != rpsui::api::Result::Ok || rpsui::api::acquire(query, worldDraw) != rpsui::api::Result::Ok) {
            spdlog::warn("RPS UI refused its interfaces; hit labels are unavailable");
            return;
        }
        if (!framework->isFrameworkReady()) {
            return;  // Retried on the next game load.
        }
        if (s_owner == 0) {
            rpsui::api::framework::ConsumerRegistration registration;
            strcpy_s(registration.consumerId, "Flying_BRICKS");
            strcpy_s(registration.displayName, "Flying BRICKS");
            rpsui::api::framework::ConsumerHandle handle;
            const auto result = framework->registerConsumer(&registration, &handle);
            if (result != rpsui::api::Result::Ok || handle.ownerToken == 0) {
                spdlog::warn("RPS UI refused the consumer ({}); hit labels are unavailable", static_cast<std::uint32_t>(result));
                return;
            }
            s_owner = handle.ownerToken;
        }
        s_draw = worldDraw;
        if (worldDraw->setFrameCallback(s_owner, &onFrame, nullptr) != rpsui::api::Result::Ok) {
            spdlog::warn("RPS UI refused the frame callback; hit labels are unavailable");
            return;
        }
        s_callbackRegistered = true;
        spdlog::info("Connected to RPS UI");
    }

    void setEnabled(bool enabled) noexcept
    {
        s_enabled.store(enabled, std::memory_order_relaxed);
    }

    void record(const float location[3], bool authored, float incoming, float afterReduction) noexcept
    {
        if (!s_enabled.load(std::memory_order_relaxed)) {
            return;
        }
        Label label;
        std::memcpy(label.location, location, sizeof(label.location));
        label.authored = authored;
        label.createdMs = GetTickCount64();
        std::snprintf(label.text, sizeof(label.text), "%s %.0f TO %.0f", authored ? "MODEL" : "NATIVE", incoming, afterReduction);

        const std::scoped_lock lock(s_lock);
        s_labels[s_nextLabel] = label;
        s_nextLabel = (s_nextLabel + 1) % s_labels.size();
    }
}
