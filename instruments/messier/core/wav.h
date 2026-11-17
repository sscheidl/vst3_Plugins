#pragma once
#include "pcm.h"
#include <atomic>

// Messier patch P2 (UPSTREAM.md): only the part of the firmware's wav.h that the
// Engine uses. The SD loader, the RIFF parser and the LED helpers are not copied.
namespace neb {
struct WavInfo { uint32_t offset=0, bytes=0, frames=0, rate=0; uint16_t channels=0; };
// Main owns this object and PCM storage, except while state is Published/Idle.
// Audio owns its view. Gate/Ack fence all destructive writes to the ONE file buffer.
struct FileBridge {
    std::atomic<bool> gate{false}, ack{false};
    std::atomic<uint32_t> generation{0};
    WavInfo published{}; // protected by generation release/acquire + gate/ack
    unsigned slot=0, bank=0;
};
}
