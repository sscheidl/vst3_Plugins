// Shared by the Elements test programs (taken from Torus Classic 0.2.0):
// checks that stay active in Release builds, an allocation counter and a few
// signal measurements.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#define CHECK(condition) \
    do { \
        if(!(condition)) { \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            std::exit(1); \
        } \
    } while(false)

namespace test_support {

// Counts the allocations of the calling thread through the replaced operator
// new below. A test reads it before and after the code that must not
// allocate; what other threads allocate meanwhile does not count. Limits: it
// sees operator new only (the aligned forms excluded), not malloc,
// VirtualAlloc or allocations inside other DLLs.
inline thread_local std::uint64_t t_allocations = 0;

struct AllocationProbe {
    std::uint64_t start = t_allocations;
    std::uint64_t Count() const { return t_allocations - start; }
};

constexpr double kPi = 3.14159265358979323846;

inline double Energy(const std::vector<float>& x, std::size_t from = 0,
                     std::size_t to = static_cast<std::size_t>(-1)) {
    double sum = 0.0;
    if(to > x.size())
        to = x.size();
    for(std::size_t i = from; i < to; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return sum;
}

inline double Rms(const std::vector<float>& x, std::size_t from = 0,
                  std::size_t to = static_cast<std::size_t>(-1)) {
    if(to > x.size())
        to = x.size();
    return to > from ? std::sqrt(Energy(x, from, to) / static_cast<double>(to - from)) : 0.0;
}

inline float Peak(const std::vector<float>& x, std::size_t from = 0) {
    float peak = 0.0f;
    for(std::size_t i = from; i < x.size(); ++i)
        peak = std::fmax(peak, std::fabs(x[i]));
    return peak;
}

// Amplitude of the component at `hz`, from a Hann-windowed single-bin DFT
// over [from, to). A full-scale sine at exactly `hz` returns 1.
template <typename Sample>
double ToneAmplitude(const std::vector<Sample>& x, double hz, double sample_rate,
                     std::size_t from, std::size_t to) {
    double re = 0.0, im = 0.0, window_sum = 0.0;
    const double length = static_cast<double>(to - from);
    for(std::size_t i = from; i < to; ++i) {
        const double n = static_cast<double>(i - from);
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * (n + 0.5) / length);
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / sample_rate;
        re += x[i] * window * std::cos(phase);
        im += x[i] * window * std::sin(phase);
        window_sum += window;
    }
    return 2.0 * std::sqrt(re * re + im * im) / window_sum;
}

// The frequency in [low, high] with the largest ToneAmplitude, searched in
// steps of `step` Hz.
template <typename Sample>
double PeakFrequency(const std::vector<Sample>& x, double low, double high, double step,
                     double sample_rate, std::size_t from, std::size_t to,
                     double* amplitude = nullptr) {
    double best = -1.0, best_hz = low;
    for(double hz = low; hz <= high; hz += step) {
        const double a = ToneAmplitude(x, hz, sample_rate, from, to);
        if(a > best) {
            best = a;
            best_hz = hz;
        }
    }
    if(amplitude)
        *amplitude = best;
    return best_hz;
}

inline double Decibel(double ratio) {
    return 20.0 * std::log10(ratio > 1e-300 ? ratio : 1e-300);
}

// A small deterministic generator for test scores (not the engine's).
struct Lcg {
    std::uint32_t state;
    explicit Lcg(std::uint32_t seed) : state(seed) {}
    std::uint32_t Next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    std::uint32_t Below(std::uint32_t n) { return Next() % n; }
    float Unit() { return static_cast<float>(Next() & 0xFFFF) / 65535.0f; }
};

} // namespace test_support

// Define TEST_SUPPORT_COUNT_ALLOCATIONS in exactly one source file of a test
// program, before including this header, to install the counting operators.
#ifdef TEST_SUPPORT_COUNT_ALLOCATIONS
void* operator new(std::size_t size) {
    ++test_support::t_allocations;
    if(void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    return operator new(size);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
#endif
