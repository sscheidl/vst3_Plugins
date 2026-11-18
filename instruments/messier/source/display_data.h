// Checks what arrives at the controller for the window's display (since 0.1.4):
// the bytes of an EngineDisplay and of a WaveOverview, sent by the processor
// (parameters.h, kDisplayMessageId). Anything out of range is refused, so the
// window never draws from a damaged message. Free of VST3 types.
#pragma once

#include "looper_engine.h"
#include "wave_overview.h"

#include <cstdint>
#include <memory>

namespace messier {

// True and the display if `size` bytes at `data` are a sound EngineDisplay.
bool ReadDisplayBytes(const void* data, std::uint32_t size, EngineDisplay& display) noexcept;

// The overview in the bytes, if they are a sound WaveOverview of the file `display`
// shows; else null (also when there is no memory for it). Never throws.
std::shared_ptr<const WaveOverview> ReadOverviewBytes(const void* data, std::uint32_t size,
                                                      const EngineDisplay& display) noexcept;

} // namespace messier
