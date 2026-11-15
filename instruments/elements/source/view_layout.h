// Geometry of the on-screen keyboard: which note lies where. Free of VSTGUI
// types, so the tests can check it. Taken from Torus Classic 0.2.0 without
// its model selector.
#pragma once

namespace elements_synth {

// Two octaves and the top C.
constexpr int kKeyboardLowNote = 60;  // C4
constexpr int kKeyboardHighNote = 84; // C6
constexpr int kKeyboardWhiteKeys = 15;
constexpr int kKeyboardVelocity = 100;

// Black keys relative to the white ones.
constexpr double kBlackKeyWidth = 0.62;  // of a white key's width
constexpr double kBlackKeyHeight = 0.6;  // of the keyboard's height

struct KeyRect {
    double left, top, right, bottom;
};

constexpr bool IsBlackKey(int note) {
    const int pitch_class = ((note % 12) + 12) % 12;
    return pitch_class == 1 || pitch_class == 3 || pitch_class == 6 || pitch_class == 8
           || pitch_class == 10;
}

// Position among the white keys, counted from the lowest C. For a black key:
// the white key left of it.
constexpr int WhiteKeyIndex(int note) {
    constexpr int kWhiteBefore[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    const int offset = note - kKeyboardLowNote;
    return offset / 12 * 7 + kWhiteBefore[offset % 12];
}

// Rectangle of a key on a keyboard of the given size, origin top left.
inline KeyRect KeyRectFor(int note, double width, double height) {
    const double white = width / kKeyboardWhiteKeys;
    const double left = WhiteKeyIndex(note) * white;
    if(!IsBlackKey(note))
        return {left, 0.0, left + white, height};
    // A black key sits on the boundary to the next white key.
    const double centre = left + white;
    const double half = white * kBlackKeyWidth / 2.0;
    return {centre - half, 0.0, centre + half, height * kBlackKeyHeight};
}

// The note at a point, or -1 outside the keyboard. Black keys lie on top.
inline int NoteAt(double x, double y, double width, double height) {
    if(!(x >= 0.0 && x < width && y >= 0.0 && y < height))
        return -1;
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        if(!IsBlackKey(note))
            continue;
        const KeyRect key = KeyRectFor(note, width, height);
        if(x >= key.left && x < key.right && y < key.bottom)
            return note;
    }
    const int index = static_cast<int>(x / (width / kKeyboardWhiteKeys));
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note)
        if(!IsBlackKey(note) && WhiteKeyIndex(note) == index)
            return note;
    return -1;
}

} // namespace elements_synth
