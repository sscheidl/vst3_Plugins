// Geometry of the on-screen keyboard: which note lies where. Free of VSTGUI
// types, so the tests can check it. Copied from Kekule (Torus Classic) 0.3.1,
// source/view_layout.h, without its model selector; only the namespace is changed.
#pragma once


namespace messier {

// Round 3 (the user's wish, provisional): one octave and the top C, C4..C5, as 13 touch
// pads (8 white notes below, 5 black ones above); before it two octaves (C4..C6). The
// count of octaves is the one place to change it.
constexpr int kKeyboardOctaves = 1;
constexpr int kKeyboardLowNote = 60; // C4
constexpr int kKeyboardHighNote = kKeyboardLowNote + 12 * kKeyboardOctaves;
constexpr int kKeyboardWhiteKeys = 7 * kKeyboardOctaves + 1;
constexpr int kKeyboardBlackKeys = 5 * kKeyboardOctaves;
constexpr int kKeyboardVelocity = 100;
// The octave keys beside the keyboard move its range by whole octaves, over the notes the
// two octaves reached before: from C2 up to C8 (MIDI 36 to 108). As in Mendelejew, it
// starts one octave down.
constexpr int kKeyboardDefaultTranspose = -12;
constexpr int kKeyboardLowestNote = 36, kKeyboardHighestNote = 108;
constexpr int kKeyboardLowestTranspose = kKeyboardLowestNote - kKeyboardLowNote;    // -24
constexpr int kKeyboardHighestTranspose = kKeyboardHighestNote - kKeyboardHighNote; // +36 for one octave

constexpr int ClampKeyboardTranspose(int semitones) {
    const int octaves = semitones / 12;
    constexpr int lowest = kKeyboardLowestTranspose / 12, highest = kKeyboardHighestTranspose / 12;
    return (octaves < lowest ? lowest : (octaves > highest ? highest : octaves)) * 12;
}

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

// The touch pads (beta 2, round 3): the black notes' pads in the upper row, centred on the
// boundary of their white keys and nearly as wide; the white notes' pads in the lower row.
// Between the black pads the upper row plays nothing.
constexpr double kPadSplit = 0.5;      // of the keyboard's height: where the lower row begins
constexpr double kBlackPadWidth = 0.9; // of a white pad's width

inline KeyRect PadRectFor(int note, double width, double height) {
    const double white = width / kKeyboardWhiteKeys;
    const double left = WhiteKeyIndex(note) * white;
    if(!IsBlackKey(note))
        return {left, height * kPadSplit, left + white, height};
    const double centre = left + white, half = white * kBlackPadWidth / 2.0;
    return {centre - half, 0.0, centre + half, height * kPadSplit};
}

inline int PadNoteAt(double x, double y, double width, double height) {
    if(!(x >= 0.0 && x < width && y >= 0.0 && y < height))
        return -1;
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        const KeyRect pad = PadRectFor(note, width, height);
        if(x >= pad.left && x < pad.right && y >= pad.top && y < pad.bottom)
            return note;
    }
    return -1;
}

} // namespace messier
