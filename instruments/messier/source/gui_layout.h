// Where things are in the window (0.2.0), in window units at 100 %: the window study,
// version 3, with the user's decisions of 2026-11-18 (STOP in the header, 15 keys at the
// key bitmap's size) and the design round of 2026-11-19 (0.2.2: SPRAY in the upper row,
// LATCH and KEYS under it, the octave keys as large as those, a lane of its own for the
// position triangle, no screws). Free of VSTGUI types, so the tests can check it.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace messier::layout {

constexpr double kWidth = 880;
constexpr double kTopHeight = 640;       // without the keyboard
constexpr double kKeyboardSection = 84;  // the chassis bitmap holds it below
constexpr double kMargin = 20;

// The header.
constexpr double kHeaderRule = 44;
constexpr double kButtonTop = 12, kButtonHeight = 26;
// STOP and the two symbol keys as one group, 6 units apart (0.2.2).
constexpr double kHeaderGap = 6;
constexpr double kPresetLeft = 822, kPresetRight = 852;
constexpr double kSizeRight = kPresetLeft - kHeaderGap, kSizeLeft = kSizeRight - 30;
constexpr double kStopRight = kSizeLeft - kHeaderGap, kStopLeft = kStopRight - 60;
constexpr double kVersionRight = kStopLeft - 14;

// The file line.
constexpr double kLoadLeft = 22, kLoadTop = 52, kLoadRight = 108, kLoadBottom = 80;
constexpr double kFileLeft = 138, kFileRight = 760;
constexpr double kFileNameTop = 48, kFileNameBottom = 70, kFileInfoBottom = 86;

// The plate view: the time axis, the plate in its bezel (since 0.2.1 with the waveform on
// the glass, the user's trial), the START slider and its row. Its x axis (the file) runs
// from kTimeLeft to kTimeRight.
constexpr double kMapLeft = 8, kMapTop = 84, kMapRight = 872, kMapBottom = 426;
constexpr double kTimeLeft = 20, kTimeRight = 860;
constexpr double kBezelEdge = 12;
// Inside the plate view, from its top: the axis labels, their ticks, the lane of the
// position triangle (0.2.2, taken from the plate's height so the window keeps its size),
// the bezel and the glass.
constexpr double kAxisBottom = 20;   // the axis labels above, then the ticks
constexpr double kMarkerTop = 21, kMarkerBottom = 30; // the position triangle's lane
constexpr double kPlateTop = 42;     // the glass: its bitmap (186 high) stretched to 232
constexpr double kPlateHeight = 232; // 0.2.0: 186, with the waveform below it; 0.2.1: 242
constexpr double kSliderTop = 290, kSliderHeight = 30;
constexpr double kRowTop = 322, kRowHeight = 18;

// The knobs: nine columns, two rows, engraved rules between.
constexpr int kColumns = 9;
constexpr double kColumnWidth = (kWidth - 2 * kMargin) / kColumns;
constexpr double kRuleAboveKnobs = 428;
constexpr double kNameTop = 434, kKnobTop = 450, kKnobSize = 72, kValueTop = 522;
constexpr double kRuleBetweenRows = 546;
constexpr double kSmallNameTop = 552, kSmallKnobTop = 566, kSmallKnobSize = 48, kSmallValueTop = 614;
constexpr double kColumnRuleX = kMargin + 8 * kColumnWidth - 2; // beside Volume, FREEZE, RESET
// The keys of the lower row, all of one size: LATCH and KEYS in the first column (0.2.2),
// FREEZE and RESET in the last; the octave keys beside the keyboard have that size too.
constexpr double kButtonWidth = 82;
constexpr double kFreezeTop = 556, kResetTop = 594;
constexpr double kLatchTop = kFreezeTop, kKeysTop = kResetTop;

constexpr double ColumnLeft(int column) {
    return kMargin + column * kColumnWidth;
}

// The keyboard section: the octave keys, 15 white keys of 36 units.
constexpr double kKeyboardRule = kTopHeight + 2;
constexpr double kPianoTop = kTopHeight + 10, kPianoHeight = 66;
constexpr double kKeyboardWidth = 15 * 36;
constexpr double kKeyboardLeft = (kWidth - kKeyboardWidth) / 2;
constexpr double kOctaveGap = 10; // between an octave key and the keyboard
constexpr double kKeyButtonTop = kPianoTop + (kPianoHeight - kButtonHeight) / 2;

constexpr double kGripSize = 18;

// A pleasant step for the seconds axis of a file of `seconds`, drawn `pixels` wide:
// labels at least `spacing` window units apart.
inline double AxisStep(double seconds, double pixels, double spacing = 64) {
    static constexpr double kSteps[] = {0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5,
                                        1,     2,     5,     10,   15,   30,   60,  120, 300, 600, 1200, 1800, 3600};
    if(!(seconds > 0) || !(pixels > 0))
        return 0;
    for(double step : kSteps)
        if(step / seconds * pixels >= spacing)
            return step;
    return 3600;
}

// The label of an axis mark: "0 s", "0.25 s", "1:30".
inline std::string AxisLabel(double seconds, double step) {
    char text[32];
    if(step >= 60) {
        const long total = std::lround(seconds);
        std::snprintf(text, sizeof text, "%ld:%02ld", total / 60, total % 60);
    } else {
        const int decimals = step >= 1 ? 0 : step >= 0.1 ? (std::fmod(step * 10, 1.0) > 1e-9 ? 2 : 1)
                                           : step >= 0.01 ? (std::fmod(step * 100, 1.0) > 1e-9 ? 3 : 2) : 3;
        std::snprintf(text, sizeof text, "%.*f s", decimals, seconds);
    }
    return text;
}

} // namespace messier::layout
