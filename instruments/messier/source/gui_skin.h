// The window's look (0.2.0): the four GUI presets (Slate, Silver, and since 0.2.5 Plum and
// Gold in the places of Copper and Photo plate), their colours, and their bitmaps. The bitmaps carry only
// surfaces and material (docs/GRAFIKAUFTRAG_GUI.md): metal, glass, nebula, knob caps,
// keys. Since 0.2.2 knob caps, thumb and key faces of Slate are drawn by
// tools/make_metal_bitmaps.py (interim, light metal); the screws are gone. Everything that carries information (text, scales, grid, waveform,
// grains, pointers) the window draws itself, in the colours below.
//
// The bitmaps lie in the bundle's Contents/Resources folder as
// messier_<preset>_<image>@1x.png and @2x.png (images shared by all presets without the
// preset in the name). A bitmap that is missing is drawn as a flat stand-in in the
// preset's colours, so the window works without them (also in the tests).
#pragma once

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawdefs.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/vstguifwd.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace messier {

// Saved with the window settings as its number; the numbers do not change.
// Round 3 (2026-11-21): the painted galaxies, one preset each; more follow at the end.
enum class GuiPreset : std::int32_t {
    kSpiral = 0,
    kBarred = 1,
    kRing = 2,
    kCloud = 3,
    kBlueGalaxy = 4,
    kOrangeGalaxy = 5,
    kRedGalaxy = 6,
    kVioletGalaxy = 7,
};
constexpr std::int32_t kGuiPresetCount = 8;
// The presets before round 3: the window settings up to version 8 stored HUE for these.
constexpr std::int32_t kLegacyGuiPresetCount = 4;

constexpr bool IsGuiPreset(std::int32_t value) {
    return value >= 0 && value < kGuiPresetCount;
}

constexpr std::int32_t kPresetAccent = -1;
// HUE is stored as a hue in degrees (0.2.5), or kPresetAccent for the preset's own colours.
// Beta 2: any hue of the circle is read (beta 1 stored 0 to 285); the slider offers red
// over violet to blue, 0 (360) down to kAccentBlueHue, and a hue outside that arc is shown
// at its nearer end.
constexpr std::int32_t kAccentMaxHue = 359;
constexpr std::int32_t kAccentBlueHue = 230;
constexpr bool IsAccent(std::int32_t hue) {
    return hue >= kPresetAccent && hue <= kAccentMaxHue;
}
// A stored hue on the slider's arc: itself if on it, else the nearer end (red or blue).
constexpr std::int32_t AccentOnScale(std::int32_t hue) {
    if(hue == 0 || hue >= kAccentBlueHue)
        return hue;
    return hue < kAccentBlueHue / 2 ? 0 : kAccentBlueHue;
}
// The slider's place (0 red, 1 blue) of a stored hue, and the hue of a place.
constexpr double AccentSliderValue(std::int32_t hue) {
    hue = AccentOnScale(hue);
    return hue == 0 ? 0.0 : (360.0 - hue) / (360.0 - kAccentBlueHue);
}
inline std::int32_t AccentOfSlider(double value) {
    const double clamped = value >= 0.0 ? (value <= 1.0 ? value : 1.0) : 0.0; // NaN: red
    const auto hue = static_cast<std::int32_t>(360.0 - clamped * (360.0 - kAccentBlueHue) + 0.5);
    return hue >= 360 ? 0 : hue;
}

VSTGUI::CColor AccentColour(double hue) noexcept;

struct SkinColours {
    VSTGUI::CColor metal, stain; // the stand-in for the chassis bitmap
    VSTGUI::CColor glass;        // the stand-in for the plate bitmap
    VSTGUI::CColor text, dim;
    // Orange (0.2.2: only the position markers): the loop's brackets and the loop in the
    // legend, the position triangle on the time axis.
    VSTGUI::CColor marker;
    VSTGUI::CColor engrave_light, engrave_dark; // the copies 1 px below and above engraved text
    VSTGUI::CColor rule_dark, rule_light;       // engraved rules
    VSTGUI::CColor grid_line, grid_cross, plate_text;
    VSTGUI::CColor nebula_a, nebula_b; // the tints of the two white nebula bitmaps
    VSTGUI::CColor grain, tail, playhead;
    VSTGUI::CColor loop_fill, spray_fill, loop_edge;
    VSTGUI::CColor wave, wave_outside;
    VSTGUI::CColor plate_wave; // the waveform on the glass (0.2.1)
    VSTGUI::CColor groove, tick, pointer;
    VSTGUI::CColor button_text;
    VSTGUI::CColor warning;
    bool negative = false; // Plum: dark marks on light glass
    // The controls (0.2.2, the user's design round): knob arcs, the START rail, the WINDOW
    // curve, pressed keys, small ornaments; and the values in it, lighter for reading.
    VSTGUI::CColor control, value;
    // Beta 2: START's fill, which HUE turns with the nebula.
    VSTGUI::CColor start_fill;
};

struct GuiPresetInfo {
    const char* name;       // as the menu shows it
    const char* file_key;   // in the names of beta 1's bitmaps and colour tables ("slate")
    const char* nebula_key; // beta 2: in the names of its nebula's two layers
    // Its bitmaps are in the bundle (all four since 0.2.4); a preset without them would
    // show as Slate and be disabled in the menu.
    bool delivered;
    SkinColours colours;
};

const GuiPresetInfo& PresetInfo(GuiPreset preset);

// The preset the window shows for a saved one: itself if its bitmaps are delivered,
// else Slate. The saved setting stays as it is.
GuiPreset ShownPreset(GuiPreset preset);

// Beta 2 (docs/GRAPHICS_BRIEF_BETA2.md): the bitmaps of the one design. Each is looked
// for under its new name and size first; for some, then under beta 1's name and size (the
// bitmaps of Slate, until the delivery). An image found under neither is drawn by code
// (the ring, the glass, the nebulae) or left out (the dome, the screws, the pads).
enum class SkinImageId {
    kChassis,
    kRing,
    kGlass,
    kDome,
    kNebula,     // the nebula variant in colour, standing; HUE turns its hue (the user's wish)
    kDustWisps,  // dark dust masks over the galaxy (black, the shape in alpha); the
    kDustVeil,   // experiment of 2026-11-20: one of them turns slowly over the nebula
    kDustBands,
    kDustClouds, // the second layer of dark clouds (the user's of 2026-11-20): white, alpha only
    kNebulaArms, // the nebula variant's two white layers, tinted by the window (first delivery)
    kNebulaHaze,
    kKnobLarge,
    kKnobSmall,
    kThumb,
    kButton,
    kButtonDown,
    kScrew,
    kPad,
    kPadLit,
    kKeyWhite,
    kKeyBlack,
    kCount
};
constexpr std::size_t kSkinImageCount = static_cast<std::size_t>(SkinImageId::kCount);

// The file name of an image of a preset (beta 2: its nebula variant) at scale 1 or 2.
std::string SkinImageFileName(GuiPreset preset, SkinImageId id, int scale);
// The size of an image in window units (its @1x pixels).
VSTGUI::CPoint SkinImageSize(SkinImageId id);
// Beta 1's name and size of an image, taken when the new one is not there; an empty name
// for an image without one.
std::string LegacySkinImageFileName(SkinImageId id, int scale);
VSTGUI::CPoint LegacySkinImageSize(SkinImageId id);

// One image in its two sizes. Either may be missing; both missing: not there. `size` is
// that of the file found (beta 1's for an old one).
struct SkinImage {
    VSTGUI::SharedPointer<VSTGUI::CBitmap> x1, x2;
    VSTGUI::CPoint size;
    explicit operator bool() const { return x1 || x2; }
};

// A copy of a white bitmap in `tint`, its alpha kept (beta 2: the nebula layers, again for
// every HUE). Null if it cannot be made. Never throws.
SkinImage TintedCopy(const SkinImage& white, VSTGUI::CColor tint) noexcept;
// The pixels of a bitmap, premultiplied, four bytes each in the platform's order, for
// turning bitmaps without Direct2D (beta 2: VSTGUI 4 of SDK 3.8 clips a turned bitmap to
// the box of two of its turned corners, which cut the nebula to wrong rectangles).
struct PixelBuffer {
    std::uint32_t width = 0, height = 0;
    double scale = 1.0; // the bitmap's scale factor
    std::vector<std::uint8_t> bytes;
    explicit operator bool() const { return width > 0 && height > 0; }
};
bool ReadPixels(VSTGUI::CBitmap* bitmap, PixelBuffer& pixels) noexcept;
VSTGUI::SharedPointer<VSTGUI::CBitmap> MakeBitmap(const PixelBuffer& pixels) noexcept;
// `from` turned clockwise by `degrees` about its centre (bilinear), transparent where
// nothing of it lies.
void TurnPixels(const PixelBuffer& from, double degrees, PixelBuffer& to);

// A copy of a coloured bitmap with every pixel's hue turned by `degrees`, its saturation,
// lightness and alpha kept (beta 2: the coloured nebula under HUE). Only the size the
// context needs (`two`: the 2x one) is made. Empty if it cannot be made. Never throws.
SkinImage HueTurnedCopy(const SkinImage& image, double degrees, bool two) noexcept;

// Custom skins (0.2.7; in 0.2.6 a test build's switch): the window takes bitmaps and
// colour tables from two folders named "Messier-Skin" before its own, file by file:
// first the user's, in Documents (writable without administrator rights), then the one
// beside the Messier.vst3 bundle (for all users of the computer). A bitmap is taken
// only at its exact pixel size (twice that for @2x); a file that is missing, unreadable,
// not an image or of another size falls through to the next folder and in the end to
// the built-in bitmap. Colour tables: the bundle folder's first, the user's on top.
std::filesystem::path UserSkinFolder();   // Documents\Messier-Skin; empty if Windows names none
std::filesystem::path BundleSkinFolder(); // beside the bundle; empty if not found
std::vector<std::filesystem::path> SkinFolders(); // the two in that order, those named
// Tests and the preview program: SkinFolders() gives exactly these from now on (an empty
// list: the built-in skin alone), whatever the computer holds. Before any window opens.
void UseSkinFolders(std::vector<std::filesystem::path> folders);
// What a custom skin may hold, as text for a file in the folder: every bitmap's name and
// pixel size, every colour's name (made from the built-in skin, so it never goes stale).
std::string SkinFolderGuide();
// Limits of the files read from a skin folder (MS-R60, MS-R61): a larger one is refused.
constexpr std::size_t kMaxSkinFileBytes = std::size_t{32} << 20;
constexpr std::size_t kMaxSkinTableBytes = std::size_t{64} << 10;
// The pixel size a PNG's first chunk declares: false unless `bytes` start with the PNG
// signature and an IHDR chunk of 13 bytes. Checked before any decoding (MS-R60).
bool SkinPngSize(const std::string& bytes, std::uint32_t& width, std::uint32_t& height);
// How many skin-folder files were handed to the image decoder so far (tests).
int SkinFilesDecoded();
// The palette menu's "Open skin folder": makes the user's folder if needed, puts the
// guide in it ("Messier skin files.txt", once) and opens it in Explorer. False if any
// step failed; never throws.
bool OpenUserSkinFolder() noexcept;
// A preset's colour table as text: one "name = #RRGGBB" or "#RRGGBBAA" per line,
// "negative = true" or "false"; lines from ";" on are comments.
std::string ColourTableFileName(GuiPreset preset); // beta 2: messier_colours.txt (beta 1's messier_slate_colours.txt is read first)
std::string ColourTableText(const SkinColours& colours);
// Takes the lines it understands onto `colours`; the count of values taken.
int ApplyColourTable(const std::string& text, SkinColours& colours);

// The look of an open window: one preset's colours, fonts and bitmaps. Made when the
// window opens and when the user picks another preset; may throw (std::bad_alloc).
class Skin {
  public:
    explicit Skin(GuiPreset preset);
    // With these skin folders, first wins (tests); the one above takes SkinFolders().
    Skin(GuiPreset preset, const std::vector<std::filesystem::path>& folders);
    // How many bitmaps and colour values came from the skin folders.
    int Overrides() const { return overrides_; }

    GuiPreset Preset() const { return preset_; }
    const SkinColours& Colours() const { return colours_; }
    // The preset's colours (with the skin folders' tables), before HUE.
    const SkinColours& BaseColours() const { return base_colours_; }
    // HUE turns the nebula and START's fill (beta 2), without reloading the bitmaps.
    void SetAccent(std::int32_t hue) noexcept;
    const SkinImage& Image(SkinImageId id) const { return images_[static_cast<std::size_t>(id)]; }

    // Engraved serif capitals: the brand and the version.
    VSTGUI::CFontRef Caps(double size) const;
    // The controls' texts (0.2.1, the user's wish): names, buttons, scales, legend. Since
    // 0.2.2 in the lighter regular weight, with letter spacing (DrawTracked).
    VSTGUI::CFontRef Sans(double size) const;
    VSTGUI::CFontRef Italic(double size) const;
    VSTGUI::CFontRef Mono(double size) const;

  private:
    GuiPreset preset_;
    SkinColours base_colours_; // the preset's, with the skin folders' tables on top
    SkinColours colours_;
    int overrides_ = 0;
    std::array<SkinImage, kSkinImageCount> images_;
    struct Font {
        int kind;
        double size;
        VSTGUI::SharedPointer<VSTGUI::CFontDesc> font;
    };
    // Made on first use and kept while the skin lives: a font handed out stays valid.
    mutable std::vector<Font> fonts_;
    VSTGUI::CFontRef FontOf(int kind, double size) const;
};

// Drawing helpers. Each takes the 1x or the 2x bitmap, whichever suits the device
// pixels the context draws to, and stretches it to `dest`.
void DrawSkinImage(VSTGUI::CDrawContext* context, const SkinImage& image, const VSTGUI::CRect& dest,
                   float alpha = 1.0f);
// A part of an image (`source` in its window units) stretched to `dest`.
void DrawSkinImagePart(VSTGUI::CDrawContext* context, const SkinImage& image, const VSTGUI::CRect& source,
                       const VSTGUI::CRect& dest, float alpha = 1.0f);
// A nine-part image: corners kept, edges and centre stretched. `edge` in window units.
// The parts meet on whole device pixels, so no seam shows between them.
void DrawNinePart(VSTGUI::CDrawContext* context, const SkinImage& image, const VSTGUI::CRect& dest,
                  VSTGUI::CCoord edge, float alpha = 1.0f);

// Text with extra space between its characters (`tracking`, window units), as the
// engraved capitals of the window have them. Vertically centred in `box`. With
// `engraved`, a light copy 1 px below and a dark copy 1 px above go first.
void DrawTracked(VSTGUI::CDrawContext* context, const std::string& text, const VSTGUI::CRect& box,
                 VSTGUI::CHoriTxtAlign align, VSTGUI::CCoord tracking, VSTGUI::CFontRef font,
                 VSTGUI::CColor colour, const SkinColours* engraved = nullptr);
// The width DrawTracked takes for this text.
VSTGUI::CCoord TrackedWidth(VSTGUI::CDrawContext* context, const std::string& text, VSTGUI::CCoord tracking,
                            VSTGUI::CFontRef font);

// An engraved rule: a dark line and a light one under it.
void DrawRule(VSTGUI::CDrawContext* context, const SkinColours& colours, VSTGUI::CPoint from, VSTGUI::CPoint to);

VSTGUI::CColor WithAlpha(VSTGUI::CColor colour, double alpha);

} // namespace messier
