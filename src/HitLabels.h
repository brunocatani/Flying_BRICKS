#pragma once

// Optional in-game diagnostic: a marker and a label at each recent hit, drawn through RPS UI's world
// drawing so it is anchored to the impact in both eyes. Off by default, and absent without RPS UI.
//
// Thread model: record() runs on hit threads, the frame callback on the render thread and the rest on
// the game's main thread. A short lock guards the label ring; the render side only tries the lock,
// so it can never stall a frame.
namespace flying_bricks::hit_labels
{
    // Safe to call again: a framework that was not ready yet is retried on the next call.
    void connect() noexcept;

    void setEnabled(bool enabled) noexcept;

    void record(const float location[3], bool authored, float incoming, float afterReduction) noexcept;
}
