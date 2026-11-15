// Torus Classic (VST3 port), 2026: not part of the upstream Torus sources.
//
// The pseudo random generator of stmlib::Random (utils/random.h, Copyright
// 2012 Emilie Gillet, MIT), with its state in an object instead of one static
// shared by the whole process. Several plug-in instances in one host process
// must not draw from the same sequence. Constants, word order and the float
// conversion are those of the original, so a Part seeded like the original
// produces the same numbers in the same order.

#ifndef TORUS_DSP_RNG_H_
#define TORUS_DSP_RNG_H_

#include <cstdint>

namespace torus
{
class Rng
{
  public:
    // stmlib::Random starts from this state when nobody seeds it, as on the
    // Daisy firmware after power-on.
    static constexpr uint32_t kPowerOnState = 0x21;

    inline void     Seed(uint32_t seed) { state_ = seed; }
    inline uint32_t state() const { return state_; }

    inline uint32_t GetWord()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    inline float GetFloat()
    {
        return static_cast<float>(GetWord()) / 4294967296.0f;
    }

  private:
    uint32_t state_ = kPowerOnState;
};

} // namespace torus

#endif // TORUS_DSP_RNG_H_
