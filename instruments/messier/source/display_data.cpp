#include "display_data.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace messier {

static_assert(std::is_trivially_copyable_v<EngineDisplay>);
static_assert(std::is_trivially_copyable_v<WaveOverview>);
static_assert(sizeof(EngineDisplay) == 536, "no padding: the bytes travel to the controller");
static_assert(sizeof(WaveOverview) == 8216, "no padding: the bytes travel to the controller");

namespace {
bool Within(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}
// Far above any speed the core reaches (4x at 768 kHz for the playhead; a grain at 32x
// two octaves up at 48 kHz is about 6 million frames a second).
constexpr double kMaxSpeed = 1e8;
// 0.2.3: a grain's window lasts at most 1000 s (the core's longest is a few seconds), so
// its end stays well inside the clock's range.
constexpr double kMinPhaseRate = 1e-3;
} // namespace

bool ReadDisplayBytes(const void* data, std::uint32_t size, EngineDisplay& display) noexcept {
    if(!data || size != sizeof(EngineDisplay))
        return false;
    EngineDisplay d;
    std::memcpy(&d, data, sizeof d);
    if(d.frozen > 1 || d.swapping > 1 || d.grain_count > EngineDisplay::kMaxGrains
       || !Within(d.head_speed, -kMaxSpeed, kMaxSpeed))
        return false;
    if(d.frames == 0) {
        // No file: nothing else counts.
        if(d.sequence != 0 || d.grain_count != 0)
            return false;
    } else {
        // Room for the rounding of the float positions of a long file (2^-23 relative).
        const double frames = d.frames, limit = frames + frames * 1e-6 + 1;
        if(d.sequence == 0 || !Within(d.loop_start, 0, frames) || !Within(d.loop_length, 0, limit)
           || !Within(d.playhead, 0, limit) || !Within(d.spray, 0, limit))
            return false;
        for(std::uint32_t i = 0; i < d.grain_count; ++i) {
            const GrainDot& g = d.grains[i];
            if(!Within(g.position, 0, limit) || !Within(g.pan, 0, 1) || !Within(g.phase, 0, 1)
               || !Within(g.speed, -kMaxSpeed, kMaxSpeed) || !(g.phase_rate == 0 || Within(g.phase_rate, kMinPhaseRate, kMaxSpeed)))
                return false;
        }
    }
    display = d;
    return true;
}

std::shared_ptr<const WaveOverview> ReadOverviewBytes(const void* data, std::uint32_t size,
                                                      const EngineDisplay& display) noexcept {
    if(!data || size != sizeof(WaveOverview) || display.frames == 0)
        return nullptr;
    // The header first, so that a wrong overview costs no allocation.
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t sequence = 0;
    std::uint32_t frames = 0, columns = 0, rate = 0;
    std::memcpy(&sequence, bytes + offsetof(WaveOverview, sequence), sizeof sequence);
    std::memcpy(&frames, bytes + offsetof(WaveOverview, frames), sizeof frames);
    std::memcpy(&columns, bytes + offsetof(WaveOverview, columns), sizeof columns);
    std::memcpy(&rate, bytes + offsetof(WaveOverview, rate), sizeof rate);
    if(sequence != display.sequence || frames != display.frames || columns == 0
       || columns > WaveOverview::kMaxColumns || columns > frames
       || (rate != 0 && (rate < 1000 || rate > 768000))) // 0.2.0: the rate, if known, is a rate
        return nullptr;
    std::shared_ptr<WaveOverview> overview;
    try {
        overview = std::make_shared<WaveOverview>();
    } catch(...) {
        return nullptr;
    }
    std::memcpy(overview.get(), data, sizeof(WaveOverview));
    for(std::uint32_t c = 0; c < overview->columns; ++c)
        if(overview->low[c] > overview->high[c])
            return nullptr;
    return overview;
}

} // namespace messier
