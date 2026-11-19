// Where things are in the window, in window units at 100 %. Since 0.2.4 the GUI standard
// of 2026-11-19 (the graphite template): one layout for all GUI presets. A compact header
// (brand, LOAD and the file, KEYS, STOP, size and preset), the plate view with START under
// it on the same x axis, the primary knobs in three groups with FREEZE and RESET beside
// them, the secondary knobs under DETAILS (closed for a new instance) and the keyboard
// section with LATCH and the octave keys. Free of VSTGUI types, so the tests can check it.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace messier::layout {

constexpr double kWidth = 880;
constexpr double kTopHeight = 560;       // without DETAILS and the keyboard
constexpr double kDetailsSection = 96;   // the secondary knobs, while DETAILS is open
constexpr double kKeyboardSection = 84;  // below everything else
constexpr double kMargin = 20;

// The height above the keyboard section.
constexpr double TopHeight(bool details) {
    return kTopHeight + (details ? kDetailsSection : 0.0);
}

// The header: brand, LOAD and the file on the left; KEYS, STOP, size and preset on the
// right, as one group 6 units apart.
constexpr double kHeaderRule = 52;
constexpr double kButtonTop = 13, kButtonHeight = 26;
constexpr double kHeaderGap = 6;
constexpr double kPresetRight = 864, kPresetLeft = kPresetRight - 30;
constexpr double kSizeRight = kPresetLeft - kHeaderGap, kSizeLeft = kSizeRight - 30;
constexpr double kStopRight = kSizeLeft - kHeaderGap, kStopLeft = kStopRight - 68;
constexpr double kKeysRight = kStopLeft - kHeaderGap, kKeysLeft = kKeysRight - 68;
constexpr double kBrandLeft = 22, kBrandStroke = 190; // the accent stroke after MESSIER
constexpr double kLoadLeft = 204, kLoadTop = kButtonTop, kLoadRight = 274, kLoadBottom = kButtonTop + kButtonHeight;
constexpr double kFileLeft = 288, kFileRight = 636, kCatalogueLeft = 548;
constexpr double kFileNameTop = 6, kFileNameBottom = 28, kFileInfoBottom = 44;

// The plate view: the time axis, the plate in its bezel (with the waveform on the glass),
// the START slider and its row. Its x axis (the file) runs from kTimeLeft to kTimeRight,
// for the plate and START alike (0.2.4: the thumb stands on the left loop bracket).
constexpr double kMapLeft = 8, kMapTop = 54, kMapRight = 872, kMapBottom = kMapTop + 342;
constexpr double kTimeLeft = 20, kTimeRight = 860;
constexpr double kBezelEdge = 12;
// Inside the plate view, from its top: the axis labels, their ticks, the lane of the
// position triangle, the bezel and the glass.
constexpr double kAxisBottom = 20;   // the axis labels above, then the ticks
constexpr double kMarkerTop = 21, kMarkerBottom = 30; // the position triangle's lane
constexpr double kPlateTop = 42;     // the glass: its bitmap (186 high) stretched to 232
constexpr double kPlateHeight = 232;
constexpr double kSliderTop = 290, kSliderHeight = 30;
constexpr double kRowTop = 322, kRowHeight = 18;

// The primary knobs: nine columns in three groups (SPRAY SPEED LOOP SIZE, DENSITY PITCH
// OVERLAP WINDOW, BLEND VOLUME), FREEZE and RESET beside the last.
constexpr double kColumnWidth = 82;
constexpr double kGroupGap = 14; // the rule between two groups stands in its middle
constexpr double kFirstColumn = 16;
constexpr double kRuleAboveKnobs = 402;
constexpr double kNameTop = 410, kKnobTop = 428, kKnobSize = 72, kValueTop = 502;
constexpr int GroupOf(int column) {
    return column < 3 ? 0 : column < 7 ? 1 : 2;
}
constexpr double ColumnLeft(int column) {
    return kFirstColumn + column * kColumnWidth + GroupOf(column) * kGroupGap;
}
constexpr double kGroupRule1 = ColumnLeft(3) - kGroupGap / 2;
constexpr double kGroupRule2 = ColumnLeft(7) - kGroupGap / 2;
// All keys of the window but the header's share one size.
constexpr double kButtonWidth = 74;
constexpr double kButtonColumnLeft = ColumnLeft(8) + kColumnWidth + 8;
constexpr double kFreezeTop = kKnobTop + 4, kResetTop = kKnobTop + 42;

// DETAILS: the bar with its switch, and below it, while open, the secondary controls.
constexpr double kDetailsBarTop = 528, kDetailsBarHeight = 24;
constexpr double kDetailsLeft = 380, kDetailsRight = 500;
constexpr double kSmallNameTop = kTopHeight + 8, kSmallKnobTop = kTopHeight + 24, kSmallKnobSize = 48,
                 kSmallValueTop = kTopHeight + 76;
constexpr int kSmallColumns = 5;
constexpr double SmallColumnLeft(int column) {
    return (kWidth - kSmallColumns * kColumnWidth) / 2 + column * kColumnWidth;
}
// Centre the spectrum slider in the space to the left of the five small knobs.
constexpr double kAccentWidth = 80;
constexpr double kAccentCentre = (kMargin + SmallColumnLeft(0)) / 2;
constexpr double kAccentLeft = kAccentCentre - kAccentWidth / 2;
constexpr double kAccentRight = kAccentCentre + kAccentWidth / 2;
constexpr double kWaveCentre = kWidth - kAccentCentre; // WAVE, as wide as HUE (0.2.6)
constexpr double kWaveLeft = kWaveCentre - kAccentWidth / 2, kWaveRight = kWaveCentre + kAccentWidth / 2;

// The keyboard section, from its top (TopHeight): LATCH, the octave keys and 15 white keys
// of 36 units, centred as one row.
constexpr double kKeyboardRule = 2;
constexpr double kPianoTop = 10, kPianoHeight = 66;
constexpr double kKeyboardWidth = 15 * 36;
constexpr double kOctaveGap = 10; // between the keys of the row
constexpr double kRowWidth = 3 * kButtonWidth + 3 * kOctaveGap + kKeyboardWidth;
constexpr double kLatchLeft = (kWidth - kRowWidth) / 2;
constexpr double kOctaveDownLeft = kLatchLeft + kButtonWidth + kOctaveGap;
constexpr double kKeyboardLeft = kOctaveDownLeft + kButtonWidth + kOctaveGap;
constexpr double kOctaveUpLeft = kKeyboardLeft + kKeyboardWidth + kOctaveGap;
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
