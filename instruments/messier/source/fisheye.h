// The round plate of 1.0 beta 2 (the user's design of 2026-11-20): the rectangle of the
// beta 1 plate (across: the share of the file; down: the panorama, L at the top) laid onto
// the round glass as through a fish-eye lens. Two steps:
//
// 1. The elliptical square-to-disc mapping: the square [-1, 1]^2 fills the unit disc, its
//    edges become the rim (the file's start the left arc, its end the right arc, L the top
//    arc, R the bottom arc). Nothing is cut off, so no round clip is needed.
// 2. A radial lens that enlarges the middle by 1 + kLens and squeezes the rim; the rim
//    stays the rim.
//
// Both have closed inverses, so a click on the glass gives back a place in the file.
// Free of VSTGUI types, so the tests can check it.
#pragma once

#include <algorithm>
#include <cmath>

namespace messier::lens {

// How much the middle of the glass is enlarged (the slope of the lens at its centre is
// 1 + kLens; at the rim 1 / (1 + kLens)).
constexpr double kLens = 0.5;

struct Point {
    double x = 0, y = 0;
};

// The square (s, t in [-1, 1]) onto the unit disc, without the lens.
inline Point SquareToDisc(double s, double t) {
    s = std::clamp(s, -1.0, 1.0);
    t = std::clamp(t, -1.0, 1.0);
    return {s * std::sqrt(1.0 - t * t / 2.0), t * std::sqrt(1.0 - s * s / 2.0)};
}

// Its inverse; a point outside the disc is taken from the rim.
inline Point DiscToSquare(double u, double v) {
    const double rho = std::hypot(u, v);
    if(rho > 1.0) {
        u /= rho;
        v /= rho;
    }
    const double root2 = std::sqrt(2.0);
    const auto half_root = [](double x) { return 0.5 * std::sqrt(std::max(0.0, x)); };
    const double a = 2.0 + u * u - v * v, b = 2.0 - u * u + v * v;
    const double s = half_root(a + 2.0 * root2 * u) - half_root(a - 2.0 * root2 * u);
    const double t = half_root(b + 2.0 * root2 * v) - half_root(b - 2.0 * root2 * v);
    return {std::clamp(s, -1.0, 1.0), std::clamp(t, -1.0, 1.0)};
}

// The lens on a radius of the unit disc, and back.
inline double Lens(double rho) {
    rho = std::clamp(rho, 0.0, 1.0);
    return rho * (1.0 + kLens) / (1.0 + kLens * rho);
}
inline double Unlens(double rho) {
    rho = std::clamp(rho, 0.0, 1.0);
    return rho / (1.0 + kLens - kLens * rho);
}

// A place of the plate onto the glass: `share` of the file (0 at its start), `down` from
// -1 (the top, L) to 1 (the bottom, R). The result is relative to the glass' centre, in
// units of its radius.
inline Point ToGlass(double share, double down) {
    const Point disc = SquareToDisc(2.0 * share - 1.0, down);
    const double rho = std::hypot(disc.x, disc.y);
    if(rho <= 0.0)
        return {0.0, 0.0};
    const double k = Lens(rho) / rho;
    return {disc.x * k, disc.y * k};
}

// A point of the glass (relative to its centre, in units of its radius) back onto the
// plate: x is the share of the file (0 to 1), y the place down (-1 to 1). Outside the
// glass: from the nearest point of the rim.
inline Point FromGlass(double x, double y) {
    double rho = std::hypot(x, y);
    if(rho > 1.0) {
        x /= rho;
        y /= rho;
        rho = 1.0;
    }
    Point disc{0.0, 0.0};
    if(rho > 0.0) {
        const double k = Unlens(rho) / rho;
        disc = {x * k, y * k};
    }
    const Point square = DiscToSquare(disc.x, disc.y);
    return {(square.x + 1.0) / 2.0, square.y};
}

// Where a grain of panorama `pan` (0 L .. 1 R) lies down the plate. Beta 1 put hard left and
// right a little inside the top and bottom (0.9); on the round glass that is its rim, so
// since the user's DAW test of beta 2 they lie well inside it.
constexpr double kPanReach = 0.55;
constexpr double PanDown(double pan) {
    return (2.0 * std::clamp(pan, 0.0, 1.0) - 1.0) * kPanReach;
}

} // namespace messier::lens
