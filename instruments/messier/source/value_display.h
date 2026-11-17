// What the host shows for a parameter value, and what typed text means. Free of
// VST3 types, so the tests can check it. Texts are UTF-8 (the "+-" sign of the
// random amounts is U+00B1); units are separate (UnitOf), as VST3 has them.
#pragma once

#include "parameters.h"
#include "value_text.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace messier {

inline const char* UnitOf(std::uint32_t id) {
    switch(id) {
        case kParamStart:
        case kParamSize:
        case kParamOverlap:
        case kParamBlend:
        case kParamSpray:
        case kParamPanRandom:
        case kParamSizeRandom:
        case kParamMask:
        case kParamVolume: return "%";
        case kParamSpeed: return "x";
        case kParamDensity: return "/s";
        case kParamPitch:
        case kParamPitchRandom: return "oct";
        case kParamDisplacement: return "s";
        case kParamPitchBend: return "st";
        default: return "";
    }
}

namespace detail {

inline std::string Printed(const char* format, double value) {
    char text[48];
    std::snprintf(text, sizeof(text), format, value);
    // A value that rounds to zero carries no minus sign.
    if(text[0] == '-' && std::strspn(text + 1, "0.") == std::strlen(text + 1))
        return text + 1;
    return text;
}

constexpr const char* kPlusMinus = "\xC2\xB1";

// The text without a leading "+-" sign, as typed for a random amount.
inline std::string_view WithoutPlusMinus(std::string_view text) {
    text = StripUnit(text, "");
    if(text.substr(0, 2) == kPlusMinus)
        text.remove_prefix(2);
    else if(text.substr(0, 2) == "+-")
        text.remove_prefix(2);
    return text;
}

inline double WindowPosition(double normalized) {
    return Clamp01(normalized) * (kWindowCount - 1);
}

} // namespace detail

inline std::string FormatValue(std::uint32_t id, double normalized) {
    using detail::Printed;
    const double v = Clamp01(normalized);
    const std::string pm = detail::kPlusMinus;
    switch(id) {
        case kParamStart: return Printed("%.1f", 100.0 * v);
        case kParamSpeed: return Printed("%+.2f", SpeedFactor(v));
        case kParamSize: return Printed("%.1f", 100.0 * SizeShare(v));
        case kParamDensity: {
            const double rate = DensityRate(v);
            return Printed(rate < 10.0 ? "%.2f" : (rate < 100.0 ? "%.1f" : "%.0f"), rate);
        }
        case kParamPitch: return Printed("%+.2f", PitchOctaves(v));
        case kParamOverlap:
        case kParamBlend:
        case kParamPanRandom:
        case kParamVolume: return Printed("%.1f", 100.0 * v);
        case kParamWindow: {
            // On a shape: its name; between two: both and how far along.
            const double position = detail::WindowPosition(v);
            const int below = static_cast<int>(position);
            const double along = position - below;
            if(below >= kWindowCount - 1 || along < 0.005)
                return kWindowNames[below < kWindowCount ? below : kWindowCount - 1];
            if(along > 0.995)
                return kWindowNames[below + 1];
            return std::string(kWindowShortNames[below]) + ">" + kWindowShortNames[below + 1] + " "
                   + Printed("%.0f", 100.0 * along) + "%";
        }
        case kParamSpray: return pm + Printed("%.1f", 100.0 * SpraySpan(v));
        case kParamDisplacement: return Printed("%.2f", DisplacementSeconds(v));
        case kParamPitchRandom: return pm + Printed("%.2f", PitchRandomOctaves(v));
        case kParamSizeRandom: return pm + Printed("%.1f", 100.0 * SizeRandomSpan(v));
        case kParamMask: return Printed("%.1f", 100.0 * MaskShare(v));
        case kParamFreeze: return IsOn(v) ? "On" : "Off";
        case kParamReset: return IsOn(v) ? "Pressed" : "Off";
        case kParamPitchBend: return Printed("%+.2f", PitchBendSemitones(v));
        default: return Printed("%.3f", v);
    }
}

// Typed text to a normalized value: one number in the parameter's unit (the unit
// itself optional), or for Window a shape's name or its number 1..7 (2.5 lies half
// way between the second and third shape), for the switches On/Off. Values beyond
// the range are pulled in. False, with `normalized` untouched, for anything else.
inline bool ParseValue(std::uint32_t id, std::string_view text, double& normalized) {
    const char* unit = UnitOf(id);
    double x = 0.0;
    switch(id) {
        case kParamFreeze:
        case kParamReset: {
            const std::string_view word = StripUnit(text, "");
            if(EqualsIgnoringCase(word, "on") || word == "1" || EqualsIgnoringCase(word, "pressed"))
                normalized = 1.0;
            else if(EqualsIgnoringCase(word, "off") || word == "0")
                normalized = 0.0;
            else
                return false;
            return true;
        }
        case kParamWindow: {
            const std::string_view name = StripUnit(text, "");
            for(int i = 0; i < kWindowCount; ++i)
                if(EqualsIgnoringCase(name, kWindowNames[i]) || EqualsIgnoringCase(name, kWindowShortNames[i])) {
                    normalized = static_cast<double>(i) / (kWindowCount - 1);
                    return true;
                }
            // As shown between two shapes: "Gauss>Blackm 40%".
            const std::size_t arrow = name.find('>');
            if(arrow != std::string_view::npos) {
                const std::string_view from = StripUnit(name.substr(0, arrow), "");
                std::string_view rest = StripUnit(name.substr(arrow + 1), "");
                const std::size_t space = rest.find(' ');
                if(space == std::string_view::npos)
                    return false;
                const std::string_view to = rest.substr(0, space);
                for(int i = 0; i + 1 < kWindowCount; ++i) {
                    const bool first = EqualsIgnoringCase(from, kWindowShortNames[i])
                                       || EqualsIgnoringCase(from, kWindowNames[i]);
                    const bool second = EqualsIgnoringCase(to, kWindowShortNames[i + 1])
                                        || EqualsIgnoringCase(to, kWindowNames[i + 1]);
                    if(first && second) {
                        if(!ParseTypedNumber(rest.substr(space + 1), "%", x))
                            return false;
                        normalized = Clamp01((i + Clamp01(x / 100.0)) / (kWindowCount - 1));
                        return true;
                    }
                }
                return false;
            }
            if(!ParseTypedNumber(text, "", x))
                return false;
            normalized = Clamp01((x - 1.0) / (kWindowCount - 1));
            return true;
        }
        case kParamSpray:
        case kParamPitchRandom:
        case kParamSizeRandom:
            if(!ParseTypedNumber(detail::WithoutPlusMinus(text), unit, x))
                return false;
            break;
        default:
            if(!ParseTypedNumber(text, unit, x))
                return false;
            break;
    }
    switch(id) {
        case kParamStart:
        case kParamOverlap:
        case kParamBlend:
        case kParamPanRandom:
        case kParamVolume: normalized = Clamp01(x / 100.0); break;
        case kParamSpeed: normalized = SpeedNormalized(x); break;
        case kParamSize: normalized = SizeNormalized(x / 100.0); break;
        case kParamDensity: normalized = DensityNormalized(x); break;
        case kParamPitch: normalized = PitchNormalized(x); break;
        case kParamSpray:
        case kParamSizeRandom: normalized = SizeNormalized(x / 100.0); break;
        case kParamDisplacement: normalized = SizeNormalized(x / 10.0); break;
        case kParamPitchRandom: normalized = Clamp01(x / 2.0); break;
        case kParamMask: normalized = Clamp01(x / 99.0); break;
        case kParamPitchBend: normalized = PitchBendNormalized(x); break;
        default: normalized = Clamp01(x); break;
    }
    return true;
}

} // namespace messier
