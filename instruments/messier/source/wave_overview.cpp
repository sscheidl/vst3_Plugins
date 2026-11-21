#include "wave_overview.h"

#include <algorithm>
#include <cmath>

namespace messier {

std::shared_ptr<WaveOverview> MakeOverview(const FileData& file) noexcept {
    if(file.frames == 0 || !file.pcm || file.channels == 0)
        return nullptr;
    std::shared_ptr<WaveOverview> overview;
    try {
        overview = std::make_shared<WaveOverview>();
    } catch(...) {
        return nullptr;
    }
    const std::uint64_t frames = file.frames;
    const std::uint32_t columns =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, WaveOverview::kMaxColumns));
    const unsigned channels = file.channels;
    const std::int16_t* pcm = file.pcm.get();
    overview->frames = file.frames;
    overview->columns = columns;
    overview->rate = file.rate;
    for(std::uint32_t c = 0; c < columns; ++c) {
        const std::uint64_t first = c * frames / columns, last = (c + 1) * frames / columns;
        std::int16_t low = pcm[first * channels], high = low;
        double squares = 0;
        for(std::uint64_t i = first * channels; i < last * channels; ++i) {
            low = std::min(low, pcm[i]);
            high = std::max(high, pcm[i]);
            squares += double(pcm[i]) * pcm[i];
        }
        overview->low[c] = low;
        overview->high[c] = high;
        const std::uint64_t count = (last - first) * channels;
        overview->rms[c] = static_cast<std::uint16_t>(
            std::min(32768.0, std::round(std::sqrt(squares / double(count > 0 ? count : 1)))));
    }
    return overview;
}

} // namespace messier
