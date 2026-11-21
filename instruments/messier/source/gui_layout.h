// Where things are in the window, in window units at 100 %. Since 1.0 beta 2 the round
// window of 2026-11-20 (the user's render): a compact header (brand, LOAD and the file,
// KEYS, STOP, size and preset), the round glass in the middle with the nine primary knobs
// on two arcs beside it and BLEND below it, the START row with FREEZE and RESET, the info
// line with DETAILS, the secondary knobs under DETAILS (closed for a new instance) and the
// keyboard section with LATCH and the octave keys. Free of VSTGUI types, so the tests can
// check it.
#pragma once

#include "view_layout.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace messier::layout {

constexpr double kWidth = 880;
constexpr double kTopHeight = 624;       // without DETAILS and the keyboard (beta 1: 560)
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
// Round 3 (the user's wish): every key with a word has one size, in the header too (LOAD,
// KEYS, STOP; FREEZE, RESET; LATCH and the octave keys). The size and preset menus share
// their own smaller one.
constexpr double kButtonWidth = 74;
constexpr double kIconButtonWidth = 30;
constexpr double kHeaderGap = 6;
constexpr double kPresetRight = 864, kPresetLeft = kPresetRight - kIconButtonWidth;
constexpr double kSizeRight = kPresetLeft - kHeaderGap, kSizeLeft = kSizeRight - kIconButtonWidth;
constexpr double kStopRight = kSizeLeft - kHeaderGap, kStopLeft = kStopRight - kButtonWidth;
constexpr double kKeysRight = kStopLeft - kHeaderGap, kKeysLeft = kKeysRight - kButtonWidth;
constexpr double kBrandLeft = 22, kBrandStroke = 190; // the accent stroke after MESSIER
constexpr double kLoadLeft = 204, kLoadTop = kButtonTop, kLoadRight = kLoadLeft + kButtonWidth,
                 kLoadBottom = kButtonTop + kButtonHeight;
// Beta 2: the file's name alone in the header, its format in the info line.
constexpr double kFileLeft = kLoadRight + 14, kFileRight = kKeysLeft - 12;
constexpr double kFileNameTop = 12, kFileNameBottom = 40;

// The round glass (beta 2): its centre, its radius and the ring around it. The plate view
// is the square around the ring with room for the seconds axis outside it.
constexpr double kGlassX = 440, kGlassY = 272;
constexpr double kGlassRadius = 180;
constexpr double kRingWidth = 16;
constexpr double kRingRadius = kGlassRadius + kRingWidth; // the ring's outer edge
constexpr double kAxisRoom = 22;                          // the seconds outside the ring
constexpr double kMapHalf = kRingRadius + kAxisRoom;
constexpr double kMapLeft = kGlassX - kMapHalf, kMapTop = kGlassY - kMapHalf;
constexpr double kMapRight = kGlassX + kMapHalf, kMapBottom = kGlassY + kMapHalf;


// The primary knobs (beta 2): their centres, four on an arc left of the glass (PITCH,
// SPEED, LOOP END, DENSITY), four mirrored on the right (SPRAY, OVERLAP, WINDOW, VOLUME),
// BLEND below the glass with its name and value under it. The arcs lie about 300 units
// from the glass' centre.
constexpr double kKnobSize = 72;
constexpr double kNameHeight = 16, kValueHeight = 16;
constexpr double kNameGap = 2, kValueGap = 4;
constexpr double kCellWidth = 90; // a knob's name and value
struct Centre {
    double x, y;
};
constexpr int kPrimaryCount = 9;
constexpr Centre kPrimary[kPrimaryCount] = {
    {177, 128}, {142, 238}, {150, 348}, {205, 458}, // left, from the top
    {703, 128}, {738, 238}, {730, 348}, {675, 458}, // right, from the top
    {kGlassX, 490},                                 // below the glass
};
constexpr int kBelowGlass = 8; // BLEND: name and value under the knob
// The boxes of a primary knob, its name and its value.
struct Box {
    double left, top, right, bottom;
};
constexpr Box KnobBox(int slot) {
    const Centre c = kPrimary[slot];
    return {c.x - kKnobSize / 2, c.y - kKnobSize / 2, c.x + kKnobSize / 2, c.y + kKnobSize / 2};
}
constexpr Box NameBox(int slot) {
    const Centre c = kPrimary[slot];
    const double top = slot == kBelowGlass ? c.y + kKnobSize / 2 + kNameGap : c.y - kKnobSize / 2 - kNameGap - kNameHeight;
    return {c.x - kCellWidth / 2, top, c.x + kCellWidth / 2, top + kNameHeight};
}
constexpr Box ValueBox(int slot) {
    const Centre c = kPrimary[slot];
    const double top = slot == kBelowGlass ? NameBox(slot).bottom + 2 : c.y + kKnobSize / 2 + kValueGap;
    return {c.x - 41, top, c.x + 41, top + kValueHeight};
}
// The scale's ends (beta 2): small texts at the lower left and right of a knob.
constexpr double kScaleWidth = 44, kScaleHeight = 12;
constexpr Box ScaleBox(int slot, bool high) {
    const Centre c = kPrimary[slot];
    const double top = c.y + kKnobSize / 2 - 10;
    return high ? Box{c.x + 24, top, c.x + 24 + kScaleWidth, top + kScaleHeight}
                : Box{c.x - 24 - kScaleWidth, top, c.x - 24, top + kScaleHeight};
}

// The START row (beta 2) and the info line below it (the file's format or what went wrong,
// the position and the mode, the maker and version). Round 5 (the user's order of the lower
// area): the START row holds only the slider, centred, with "START" left of it and "END"
// right of it (the loop's end thumb); FREEZE and RESET stand above it on the right, below
// VOLUME; DETAILS went to the top left corner (kDetailsBox).
constexpr double kStartTop = 566, kStartHeight = 32;
constexpr double kInfoTop = 602, kInfoHeight = 22;
constexpr double kThumbRoom = 12; // the START bar's thumbs reach this far beyond its track
constexpr double kButtonGap = 12;
constexpr double kStartNameWidth = 80; // "START" (and "END") and the gap to the track
struct BottomRows {
    Box details, start_bar, freeze, reset;
    double track_left, track_right; // inside the START bar: the track, between the names
    Box file_info, status;
    double version_left;
};
constexpr Box ButtonAt(double left, double top) {
    return {left, top, left + kButtonWidth, top + kButtonHeight};
}
constexpr double kStartKeysTop = 534; // FREEZE and RESET
constexpr BottomRows Bottom() {
    BottomRows rows{};
    rows.details = ButtonAt(kMargin, 64);
    rows.reset = ButtonAt(kWidth - kMargin - kButtonWidth, kStartKeysTop);
    rows.freeze = ButtonAt(rows.reset.left - kButtonGap - kButtonWidth, kStartKeysTop);
    rows.start_bar = {kMargin, kStartTop, kWidth - kMargin, kStartTop + kStartHeight};
    rows.track_left = kStartNameWidth;
    rows.track_right = (kWidth - 2 * kMargin) - kStartNameWidth; // the track centred: 100 to 780
    rows.file_info = {kMargin, kInfoTop, 540, kInfoTop + kInfoHeight};
    rows.status = {548, kInfoTop, 712, kInfoTop + kInfoHeight};
    rows.version_left = 716;
    return rows;
}

// GALAXY / WAVE (round 5, the user's wish): two half-arcs round the BLEND knob, behind it
// and outside its value arc, "GALAXY" on the left, "WAVE" on the right, each with its name
// beside it. Angles in degrees clockwise from twelve o'clock; the arcs stay clear of the
// glass' ring above and of BLEND's scale texts below.
constexpr double kArcRadius = 50, kArcWidth = 5;
constexpr double kGalaxyArcFrom = 245, kGalaxyArcTo = 295, kWaveArcFrom = 65, kWaveArcTo = 115;
constexpr double kArcNameWidth = 58, kArcNameGap = 8;
constexpr Box ArcSwitchBox() {
    const Centre c = kPrimary[kBelowGlass];
    const double reach = kArcRadius + kArcWidth / 2 + kArcNameGap + kArcNameWidth;
    return {c.x - reach, c.y - 25, c.x + reach, c.y + 25};
}

// DETAILS: below the info line, while open, the secondary controls.
constexpr double kSmallNameTop = kTopHeight + 8, kSmallKnobTop = kTopHeight + 24, kSmallKnobSize = 48,
                 kSmallValueTop = kTopHeight + 76;
constexpr double kColumnWidth = 82;
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

// The keyboard section, from its top (TopHeight): LATCH, the octave keys and the keys,
// centred as one row. Round 3: one octave of pads, 52 units each (before: 15 keys of 36).
constexpr double kKeyboardRule = 2;
constexpr double kPianoTop = 10, kPianoHeight = 66;
constexpr double kPadWidth = 52;
constexpr double kKeyboardWidth = kKeyboardWhiteKeys * kPadWidth;
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
