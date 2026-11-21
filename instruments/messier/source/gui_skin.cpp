#include "gui_skin.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/cresourcedescription.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include "vstgui/lib/platform/platformfactory.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

namespace messier {

using namespace VSTGUI;

namespace {

constexpr CColor Rgb(int r, int g, int b) {
    return CColor(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}
constexpr CColor Rgba(int r, int g, int b, double a) {
    return CColor(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b),
                  static_cast<uint8_t>(a * 255.0 + 0.5));
}

// The dark design (beta 2: the only one; the user's decision of 2026-11-20). Its colours are
// those of Slate, the graphite standard of the GUI template of 0.2.4.
const SkinColours kDark = {
    .metal = Rgb(21, 23, 28),
    .stain = Rgb(38, 44, 56),
    .glass = Rgb(13, 17, 32),
    .text = Rgb(242, 241, 239),
    .dim = Rgb(180, 190, 208),
    .marker = Rgb(255, 170, 88),
    .engrave_light = Rgba(255, 255, 255, 0.04705882),
    .engrave_dark = Rgba(8, 10, 16, 0.56470588),
    .rule_dark = Rgba(8, 11, 20, 0.63921569),
    .rule_light = Rgba(186, 196, 223, 0.15686275),
    .grid_line = Rgba(173, 189, 239, 0.10980392),
    .grid_cross = Rgba(196, 212, 251, 0.37647059),
    .plate_text = Rgb(221, 230, 250),
    .nebula_a = Rgb(113, 83, 178),
    .nebula_b = Rgb(69, 57, 134),
    .grain = Rgb(255, 244, 228),
    .tail = Rgb(255, 200, 152),
    .playhead = Rgb(242, 241, 239),
    .loop_fill = Rgba(156, 168, 239, 0.09019608),
    .spray_fill = Rgba(188, 198, 245, 0.12156863),
    .loop_edge = Rgba(255, 170, 88, 0.58039216),
    .wave = Rgb(217, 212, 247),
    .wave_outside = Rgb(146, 141, 185),
    .plate_wave = Rgb(182, 175, 240),
    .groove = Rgba(9, 13, 23, 0.67450980),
    .tick = Rgba(186, 196, 225, 0.72156863),
    .pointer = Rgb(242, 241, 239),
    .button_text = Rgb(242, 241, 239),
    .warning = Rgb(255, 182, 130),
    .negative = false,
    .control = Rgb(165, 159, 242),
    .value = Rgb(200, 197, 254),
    .start_fill = Rgb(165, 159, 242),
};

// The design with another nebula's tints.
SkinColours WithNebula(CColor a, CColor b) {
    SkinColours colours = kDark;
    colours.nebula_a = a;
    colours.nebula_b = b;
    return colours;
}

// Beta 2: the four nebula variants take the places (and stored IDs) of the GUI presets
// Slate, Silver, Plum and Gold, each with its own tints (what HUE turns): the spiral violet,
// the barred spiral blue, the ring nebula rose violet, the cloud rose. All use the dark
// design's bitmaps.
const GuiPresetInfo kPresets[kGuiPresetCount] = {
    {"Spiral galaxy (M51)", "slate", "spiral", true, kDark},
    {"Barred spiral (M109)", "slate", "barred", true, WithNebula(Rgb(88, 110, 172), Rgb(50, 59, 115))},
    {"Ring nebula (M57)", "slate", "ring", true, WithNebula(Rgb(150, 86, 160), Rgb(84, 56, 128))},
    {"Diffuse nebula (M42)", "slate", "cloud", true, WithNebula(Rgb(178, 92, 120), Rgb(104, 52, 96))},
    // Round 3: the painted galaxies (messier_nebula_<key>, in colour). The first tint has
    // the painting's own main hue, so that HUE turns the painting to the hue it shows.
    {"Blue galaxy", "slate", "blue", true, WithNebula(Rgb(64, 130, 220), Rgb(24, 60, 130))},
    {"Orange galaxy", "slate", "orange", true, WithNebula(Rgb(222, 128, 66), Rgb(130, 60, 30))},
    {"Red galaxy", "slate", "red", true, WithNebula(Rgb(206, 56, 92), Rgb(110, 24, 48))},
    {"Violet galaxy", "slate", "violet", true, WithNebula(Rgb(158, 84, 214), Rgb(84, 40, 128))},
};

struct ImageSpec {
    const char* stem; // "nebula_<variant>_" goes before it for the nebula layers
    bool per_variant;
    int width, height;
    const char* legacy; // beta 1's name (GRAFIKAUFTRAG_GUI.md), or null
    int legacy_width, legacy_height;
};

// Beta 2: docs/GRAPHICS_BRIEF_BETA2.md; beta 1's bitmaps of Slate where they still fit.
// The keys keep their names (keys_white, keys_black).
constexpr ImageSpec kImages[kSkinImageCount] = {
    {"chassis", false, 880, 804, "slate_chassis", 880, 724},
    {"ring", false, 432, 432, nullptr, 0, 0},
    {"glass", false, 360, 360, nullptr, 0, 0},
    {"dome", false, 360, 360, nullptr, 0, 0},
    {"", true, 360, 360, nullptr, 0, 0}, // messier_nebula_<variant>
    {"dust_wisps_dark", false, 360, 360, nullptr, 0, 0},
    {"dust_veil_dark", false, 360, 360, nullptr, 0, 0},
    {"dust_bands_dark", false, 360, 360, nullptr, 0, 0},
    {"dust_clouds", false, 360, 360, nullptr, 0, 0},
    {"arms", true, 360, 360, nullptr, 0, 0},
    {"haze", true, 360, 360, nullptr, 0, 0},
    {"knob_l", false, 72, 72, "slate_knob_l", 72, 72},
    {"knob_s", false, 48, 48, "slate_knob_s", 48, 48},
    {"thumb", false, 24, 24, "slate_thumb", 16, 28},
    {"button", false, 48, 28, "slate_button", 48, 28},
    {"button_down", false, 48, 28, "slate_button_down", 48, 28},
    {"screw", false, 14, 14, nullptr, 0, 0},
    {"pad", false, 40, 40, nullptr, 0, 0},
    {"pad_lit", false, 40, 40, nullptr, 0, 0},
    {"keys_white", false, 36, 66, nullptr, 0, 0},
    {"keys_black", false, 22, 40, nullptr, 0, 0},
};

// The bitmap of `image` that suits the context: the 2x one above 100 % device pixels.
// Beta 2: the scale also under a rotation (the nebula layers turn).
CBitmap* Pick(CDrawContext* context, const SkinImage& image) {
    const CGraphicsTransform t = context->getCurrentTransform();
    const double scale = context->getScaleFactor() * std::hypot(t.m11, t.m12);
    if(scale > 1.01 && image.x2)
        return image.x2;
    return image.x1 ? image.x1.get() : image.x2.get();
}

// A coordinate of the context moved onto the nearest device pixel.
CCoord SnapX(CDrawContext* context, CCoord x) {
    const CGraphicsTransform t = context->getCurrentTransform();
    const double s = context->getScaleFactor();
    if(t.m11 == 0.0 || s <= 0.0)
        return x;
    const double device = std::round((t.m11 * x + t.dx) * s);
    return (device / s - t.dx) / t.m11;
}

CCoord SnapY(CDrawContext* context, CCoord y) {
    const CGraphicsTransform t = context->getCurrentTransform();
    const double s = context->getScaleFactor();
    if(t.m22 == 0.0 || s <= 0.0)
        return y;
    const double device = std::round((t.m22 * y + t.dy) * s);
    return (device / s - t.dy) / t.m22;
}

// A copy of a white bitmap in `tint`, its alpha kept.
SharedPointer<CBitmap> TintedBitmap(CBitmap* white, CColor tint) {
    if(!white || !white->getPlatformBitmap())
        return nullptr;
    const PlatformBitmapPtr& source = white->getPlatformBitmap();
    PlatformBitmapPtr copy = getPlatformFactory().createBitmap(source->getSize());
    if(!copy)
        return nullptr;
    copy->setScaleFactor(source->getScaleFactor());
    auto result = makeOwned<CBitmap>(copy);
    auto from = owned(CBitmapPixelAccess::create(white, false));
    auto to = owned(CBitmapPixelAccess::create(result, false));
    if(!from || !to)
        return nullptr;
    CColor c;
    do {
        from->getColor(c);
        to->setColor(CColor(tint.red, tint.green, tint.blue, c.alpha));
    } while(++(*from) && ++(*to));
    return result;
}

// The UTF-8 characters of a text, one string each.
std::vector<std::string> Characters(const std::string& text) {
    std::vector<std::string> out;
    for(std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 1;
        length = std::min(length, text.size() - i);
        out.push_back(text.substr(i, length));
        i += length;
    }
    return out;
}

} // namespace

const GuiPresetInfo& PresetInfo(GuiPreset preset) {
    const auto index = static_cast<std::int32_t>(preset);
    return kPresets[IsGuiPreset(index) ? index : 0];
}

GuiPreset ShownPreset(GuiPreset preset) {
    const auto index = static_cast<std::int32_t>(preset);
    return IsGuiPreset(index) && kPresets[index].delivered ? preset : GuiPreset::kSpiral;
}

std::string SkinImageFileName(GuiPreset preset, SkinImageId id, int scale) {
    const ImageSpec& spec = kImages[static_cast<std::size_t>(id)];
    std::string name = "messier_";
    if(spec.per_variant) {
        name += "nebula_";
        name += PresetInfo(preset).nebula_key;
        if(*spec.stem)
            name += '_';
    }
    name += spec.stem;
    name += scale == 2 ? "@2x.png" : "@1x.png";
    return name;
}

CPoint SkinImageSize(SkinImageId id) {
    const ImageSpec& spec = kImages[static_cast<std::size_t>(id)];
    return CPoint(spec.width, spec.height);
}

std::string LegacySkinImageFileName(SkinImageId id, int scale) {
    const ImageSpec& spec = kImages[static_cast<std::size_t>(id)];
    if(!spec.legacy)
        return {};
    return std::string("messier_") + spec.legacy + (scale == 2 ? "@2x.png" : "@1x.png");
}

CPoint LegacySkinImageSize(SkinImageId id) {
    const ImageSpec& spec = kImages[static_cast<std::size_t>(id)];
    return CPoint(spec.legacy_width, spec.legacy_height);
}

namespace {
SharedPointer<CBitmap> HueTurnedBitmap(CBitmap* image, double degrees) {
    if(!image || !image->getPlatformBitmap())
        return nullptr;
    const PlatformBitmapPtr& source = image->getPlatformBitmap();
    PlatformBitmapPtr copy = getPlatformFactory().createBitmap(source->getSize());
    if(!copy)
        return nullptr;
    copy->setScaleFactor(source->getScaleFactor());
    auto result = makeOwned<CBitmap>(copy);
    auto from = owned(CBitmapPixelAccess::create(image, false));
    auto to = owned(CBitmapPixelAccess::create(result, false));
    if(!from || !to)
        return nullptr;
    CColor c;
    do {
        from->getColor(c);
        if(c.alpha > 0) {
            double h = 0, s = 0, l = 0;
            c.toHSL(h, s, l);
            const uint8_t alpha = c.alpha;
            c.fromHSL(std::fmod(h + degrees + 720.0, 360.0), s, l);
            c.alpha = alpha;
        }
        to->setColor(c);
    } while(++(*from) && ++(*to));
    return result;
}
} // namespace

SkinImage HueTurnedCopy(const SkinImage& image, double degrees, bool two) noexcept {
    SkinImage turned;
    try {
        turned.size = image.size;
        CBitmap* source = two && image.x2 ? image.x2.get() : image.x1 ? image.x1.get() : image.x2.get();
        auto bitmap = HueTurnedBitmap(source, degrees);
        if(source == image.x2.get())
            turned.x2 = bitmap;
        else
            turned.x1 = bitmap;
    } catch(...) {
        return {};
    }
    return turned;
}

bool ReadPixels(CBitmap* bitmap, PixelBuffer& pixels) noexcept {
    try {
        pixels = {};
        if(!bitmap || !bitmap->getPlatformBitmap())
            return false;
        auto access = owned(CBitmapPixelAccess::create(bitmap, true));
        if(!access || !access->getPlatformBitmapPixelAccess())
            return false;
        auto* platform = access->getPlatformBitmapPixelAccess();
        pixels.width = access->getBitmapWidth();
        pixels.height = access->getBitmapHeight();
        pixels.scale = bitmap->getPlatformBitmap()->getScaleFactor();
        pixels.bytes.resize(std::size_t(pixels.width) * pixels.height * 4);
        const std::uint8_t* address = platform->getAddress();
        const std::uint32_t row = platform->getBytesPerRow();
        for(std::uint32_t y = 0; y < pixels.height; ++y)
            std::memcpy(pixels.bytes.data() + std::size_t(y) * pixels.width * 4, address + std::size_t(y) * row,
                        std::size_t(pixels.width) * 4);
        return true;
    } catch(...) {
        pixels = {};
        return false;
    }
}

SharedPointer<CBitmap> MakeBitmap(const PixelBuffer& pixels) noexcept {
    try {
        if(!pixels)
            return nullptr;
        PlatformBitmapPtr platform = getPlatformFactory().createBitmap(CPoint(pixels.width, pixels.height));
        if(!platform)
            return nullptr;
        platform->setScaleFactor(pixels.scale);
        auto bitmap = makeOwned<CBitmap>(platform);
        {
            auto access = owned(CBitmapPixelAccess::create(bitmap, true));
            if(!access || !access->getPlatformBitmapPixelAccess())
                return nullptr;
            auto* target = access->getPlatformBitmapPixelAccess();
            std::uint8_t* address = target->getAddress();
            const std::uint32_t row = target->getBytesPerRow();
            for(std::uint32_t y = 0; y < pixels.height; ++y)
                std::memcpy(address + std::size_t(y) * row, pixels.bytes.data() + std::size_t(y) * pixels.width * 4,
                            std::size_t(pixels.width) * 4);
        } // the accessor goes first: only then does the bitmap show the pixels
        return bitmap;
    } catch(...) {
        return nullptr;
    }
}

void TurnPixels(const PixelBuffer& from, double degrees, PixelBuffer& to) {
    // MS-R63 (the GUI review of beta 2): bilinear in 16.16 fixed point, stepping along each
    // row, over the inscribed disc only; `to` keeps its buffer from the call before.
    if(to.width != from.width || to.height != from.height || to.bytes.size() != from.bytes.size())
        to.bytes.assign(from.bytes.size(), 0);
    to.width = from.width;
    to.height = from.height;
    to.scale = from.scale;
    if(!from)
        return;
    const double angle = degrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(angle), s = std::sin(angle);
    const int w = static_cast<int>(from.width), h = static_cast<int>(from.height);
    const double cx = w / 2.0, cy = h / 2.0, radius = std::min(w, h) / 2.0 + 1.0;
    constexpr int kShift = 16;
    constexpr double kOne = 1 << kShift;
    const std::int64_t step_x = std::llround(c * kOne), step_y = std::llround(-s * kOne);
    const std::uint8_t* src = from.bytes.data();
    const auto pixel = [&](std::int64_t x, std::int64_t y) -> const std::uint8_t* {
        static constexpr std::uint8_t kClear[4] = {0, 0, 0, 0};
        return x < 0 || y < 0 || x >= w || y >= h ? kClear : src + (std::size_t(y) * w + x) * 4;
    };
    for(int y = 0; y < h; ++y) {
        std::uint8_t* row = to.bytes.data() + std::size_t(y) * w * 4;
        const double dy = y + 0.5 - cy;
        const double inside = radius * radius - dy * dy;
        const double span = inside > 0 ? std::sqrt(inside) : -1.0;
        const int x_from = std::clamp(static_cast<int>(std::floor(cx - span)), 0, w);
        const int x_to = std::clamp(static_cast<int>(std::ceil(cx + span)), x_from, w);
        std::memset(row, 0, std::size_t(x_from) * 4);
        std::memset(row + std::size_t(x_to) * 4, 0, std::size_t(w - x_to) * 4);
        // Where the row's first pixel centre comes from, turned back; then one step a pixel.
        const double dx = x_from + 0.5 - cx;
        std::int64_t fx = std::llround((c * dx + s * dy + cx - 0.5) * kOne);
        std::int64_t fy = std::llround((-s * dx + c * dy + cy - 0.5) * kOne);
        for(int x = x_from; x < x_to; ++x, fx += step_x, fy += step_y) {
            const std::int64_t x0 = fx >> kShift, y0 = fy >> kShift; // floor, also below 0
            const std::uint32_t wx = static_cast<std::uint32_t>((fx >> (kShift - 8)) & 0xFF);
            const std::uint32_t wy = static_cast<std::uint32_t>((fy >> (kShift - 8)) & 0xFF);
            std::uint8_t* out = row + std::size_t(x) * 4;
            if(x0 < -1 || y0 < -1 || x0 >= w || y0 >= h) {
                out[0] = out[1] = out[2] = out[3] = 0;
                continue;
            }
            const bool inner = x0 >= 0 && y0 >= 0 && x0 + 1 < w && y0 + 1 < h;
            const std::uint8_t* p00 = inner ? src + (std::size_t(y0) * w + x0) * 4 : pixel(x0, y0);
            const std::uint8_t* p10 = inner ? p00 + 4 : pixel(x0 + 1, y0);
            const std::uint8_t* p01 = inner ? p00 + std::size_t(w) * 4 : pixel(x0, y0 + 1);
            const std::uint8_t* p11 = inner ? p01 + 4 : pixel(x0 + 1, y0 + 1);
            for(int k = 0; k < 4; ++k) {
                const std::uint32_t top = p00[k] * (256 - wx) + p10[k] * wx;
                const std::uint32_t bottom = p01[k] * (256 - wx) + p11[k] * wx;
                out[k] = static_cast<std::uint8_t>((top * (256 - wy) + bottom * wy + 32768) >> 16);
            }
        }
    }
}

SkinImage TintedCopy(const SkinImage& white, CColor tint) noexcept {
    SkinImage tinted;
    try {
        tinted.size = white.size;
        tinted.x1 = TintedBitmap(white.x1, tint);
        tinted.x2 = TintedBitmap(white.x2, tint);
    } catch(...) {
        return {};
    }
    return tinted;
}

namespace {

struct ColourField {
    const char* name;
    CColor SkinColours::*member;
};
constexpr ColourField kColourFields[] = {
    {"metal", &SkinColours::metal},          {"stain", &SkinColours::stain},
    {"glass", &SkinColours::glass},          {"text", &SkinColours::text},
    {"dim", &SkinColours::dim},              {"marker", &SkinColours::marker},
    {"engrave_light", &SkinColours::engrave_light}, {"engrave_dark", &SkinColours::engrave_dark},
    {"rule_dark", &SkinColours::rule_dark},  {"rule_light", &SkinColours::rule_light},
    {"grid_line", &SkinColours::grid_line},  {"grid_cross", &SkinColours::grid_cross},
    {"plate_text", &SkinColours::plate_text}, {"nebula_a", &SkinColours::nebula_a},
    {"nebula_b", &SkinColours::nebula_b},    {"grain", &SkinColours::grain},
    {"tail", &SkinColours::tail},            {"playhead", &SkinColours::playhead},
    {"loop_fill", &SkinColours::loop_fill},  {"spray_fill", &SkinColours::spray_fill},
    {"loop_edge", &SkinColours::loop_edge},  {"wave", &SkinColours::wave},
    {"wave_outside", &SkinColours::wave_outside}, {"plate_wave", &SkinColours::plate_wave},
    {"groove", &SkinColours::groove},        {"tick", &SkinColours::tick},
    {"pointer", &SkinColours::pointer},      {"button_text", &SkinColours::button_text},
    {"warning", &SkinColours::warning},      {"control", &SkinColours::control},
    {"value", &SkinColours::value},          {"start_fill", &SkinColours::start_fill},
};

std::string Trim(std::string text) {
    while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    std::size_t start = 0;
    while(start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    return text.substr(start);
}

bool ParseHex(const std::string& text, CColor& colour) {
    if(text.size() != 7 && text.size() != 9)
        return false;
    if(text[0] != '#')
        return false;
    unsigned value[4] = {0, 0, 0, 255};
    for(std::size_t k = 0; k + 1 < text.size(); k += 2) {
        unsigned byte = 0;
        for(std::size_t d = 1 + k; d < 3 + k; ++d) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[d])));
            if(c >= '0' && c <= '9')
                byte = byte * 16 + static_cast<unsigned>(c - '0');
            else if(c >= 'a' && c <= 'f')
                byte = byte * 16 + static_cast<unsigned>(c - 'a' + 10);
            else
                return false;
        }
        value[k / 2] = byte;
    }
    colour = CColor(static_cast<uint8_t>(value[0]), static_cast<uint8_t>(value[1]), static_cast<uint8_t>(value[2]),
                    static_cast<uint8_t>(value[3]));
    return true;
}

// At most `limit` bytes of a file, read from the open stream in steps; false if the file
// is larger, unreadable or not a regular file. A file that grows or is replaced after
// it was opened cannot bring more (MS-R60, MS-R61). Never throws.
bool ReadLimited(const std::filesystem::path& file, std::size_t limit, std::string& bytes) noexcept {
    try {
        std::error_code error;
        if(file.empty() || !std::filesystem::is_regular_file(file, error))
            return false;
        std::ifstream in(file, std::ios::binary);
        if(!in)
            return false;
        bytes.clear();
        char step[65536];
        while(in) {
            in.read(step, sizeof step);
            const auto got = static_cast<std::size_t>(in.gcount());
            if(got > limit - bytes.size()) // one byte past the limit is enough to refuse
                return false;
            bytes.append(step, got);
        }
        return !in.bad();
    } catch(...) {
        return false;
    }
}

std::atomic<int> g_decoded{0};

// The first of `folders` with a PNG `name` of exactly `size` pixels (MS-R60): at most
// kMaxSkinFileBytes read, the signature and the IHDR chunk checked against `size`, and
// only then exactly those bytes handed to the decoder, from memory. Anything else falls
// through to the next folder.
PlatformBitmapPtr BitmapFromFolders(const std::vector<std::filesystem::path>& folders, const std::string& name,
                                    CPoint size) noexcept {
    for(const std::filesystem::path& folder : folders) {
        try {
            std::string bytes;
            std::uint32_t width = 0, height = 0;
            if(!ReadLimited(folder / name, kMaxSkinFileBytes, bytes) || !SkinPngSize(bytes, width, height)
               || width != static_cast<std::uint32_t>(size.x) || height != static_cast<std::uint32_t>(size.y))
                continue;
            ++g_decoded;
            PlatformBitmapPtr bitmap =
                getPlatformFactory().createBitmapFromMemory(bytes.data(), static_cast<uint32_t>(bytes.size()));
            // C7 (0.2.9): the platform decodes lazily; one full decode now, so that a file
            // with broken pixel data falls through instead of drawing as nothing.
            if(bitmap && bitmap->getSize() == size && bitmap->lockPixels(true))
                return bitmap;
        } catch(...) {
        }
    }
    return nullptr;
}

// A colour table's text, or nothing: missing, unreadable, larger than kMaxSkinTableBytes.
bool ReadSkinTable(const std::filesystem::path& file, std::string& text) noexcept {
    return ReadLimited(file, kMaxSkinTableBytes, text);
}

} // namespace

bool SkinPngSize(const std::string& bytes, std::uint32_t& width, std::uint32_t& height) {
    static constexpr unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if(bytes.size() < 33 || std::memcmp(bytes.data(), kSignature, 8) != 0)
        return false;
    const auto be = [&](std::size_t at) {
        const auto* b = reinterpret_cast<const unsigned char*>(bytes.data()) + at;
        return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) | b[3];
    };
    if(be(8) != 13 || std::memcmp(bytes.data() + 12, "IHDR", 4) != 0)
        return false;
    width = be(16);
    height = be(20);
    return true;
}

int SkinFilesDecoded() {
    return g_decoded.load();
}

std::filesystem::path UserSkinFolder() {
    try {
        PWSTR documents = nullptr;
        std::filesystem::path folder;
        if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents)) && documents)
            folder = std::filesystem::path(documents) / "Messier-Skin";
        CoTaskMemFree(documents);
        return folder;
    } catch(...) {
        return {};
    }
}

std::filesystem::path BundleSkinFolder() {
    // This module's file: ...\Messier.vst3\Contents\x86_64-win\Messier.vst3; the folder
    // sits beside the bundle.
    HMODULE module = nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&BundleSkinFolder), &module))
        return {};
    wchar_t buffer[32768];
    const DWORD length = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
    if(length == 0 || length >= std::size(buffer))
        return {};
    std::filesystem::path file(std::wstring(buffer, length));
    std::filesystem::path bundle = file.parent_path().parent_path().parent_path();
    // Outside a bundle (a test program): beside the program.
    if(bundle.extension() != ".vst3")
        bundle = file;
    return bundle.parent_path() / "Messier-Skin";
}

namespace {
bool g_folders_fixed = false;
std::vector<std::filesystem::path> g_fixed_folders;
} // namespace

void UseSkinFolders(std::vector<std::filesystem::path> folders) {
    g_fixed_folders = std::move(folders);
    g_folders_fixed = true;
}

std::vector<std::filesystem::path> SkinFolders() {
    if(g_folders_fixed)
        return g_fixed_folders;
    std::vector<std::filesystem::path> folders;
    for(std::filesystem::path folder : {UserSkinFolder(), BundleSkinFolder()})
        if(!folder.empty())
            folders.push_back(std::move(folder));
    return folders;
}

std::string SkinFolderGuide() {
    std::string text =
        "Messier custom skin\n"
        "===================\n\n"
        "Messier takes the files below from this folder before its own, file by file. A file\n"
        "that is missing, unreadable or not exactly the size given is replaced by the built-in\n"
        "one. Palette menu > \"Reload skin folders\" reads the folders again.\n\n"
        "Folders, the first that has a file wins:\n"
        "  1. Messier-Skin in your Documents folder\n"
        "  2. Messier-Skin beside Messier.vst3 (for all users; needs administrator rights)\n\n"
        "Bitmaps: PNG, the size below for @1x and exactly twice that for @2x.\n";
    // B2C.2: every preset's key, and the names exactly as the skin looks for them.
    text += "<nebula> is one of:";
    for(const GuiPresetInfo& preset : kPresets)
        text += std::string(" ") + preset.nebula_key;
    text += "\n\n";
    char line[128];
    for(std::size_t i = 0; i < kSkinImageCount; ++i) {
        const ImageSpec& spec = kImages[i];
        std::string name = SkinImageFileName(GuiPreset::kSpiral, static_cast<SkinImageId>(i), 1);
        if(spec.per_variant)
            name.replace(name.find("spiral"), 6, "<nebula>");
        std::snprintf(line, sizeof line, "  %-36s %4d x %-4d   @2x.png: %d x %d\n", name.c_str(), spec.width,
                      spec.height, spec.width * 2, spec.height * 2);
        text += line;
    }
    text += "\nThe nebula layers are white; HUE and the colours nebula_a (arms) and nebula_b (haze)\n"
            "tint them; the painted galaxies (messier_nebula_<nebula>@1x.png) are in colour and\n"
            "HUE turns their hue. Bitmaps of beta 1 (messier_slate_*) are still taken where they\n"
            "fit; beta 1's colour table messier_slate_colours.txt only under the spiral.\n\n"
            "Colour tables: messier_colours.txt, one line per colour,\n"
            "  name = #RRGGBB or #RRGGBBAA (AA: opacity); from \";\" on a comment.\n"
            "A line left out keeps the built-in colour. The tables of both folders apply;\n"
            "where both set a colour, the one in Documents wins. Names:\n";
    for(const ColourField& field : kColourFields) {
        text += "  ";
        text += field.name;
        text += "\n";
    }
    text += "  negative = true or false\n"
            "Read but without effect so far: negative, wave, wave_outside, loop_edge.\n";
    return text;
}

bool OpenUserSkinFolder() noexcept {
    try {
        const std::filesystem::path folder = UserSkinFolder();
        std::error_code error;
        if(folder.empty() || (!std::filesystem::create_directories(folder, error) && error))
            return false;
        const std::filesystem::path guide = folder / "Messier skin files.txt";
        if(!std::filesystem::exists(guide, error)) {
            std::ofstream out(guide, std::ios::binary);
            out << SkinFolderGuide();
        }
        const auto result = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        return result > 32;
    } catch(...) {
        return false;
    }
}

std::string ColourTableFileName(GuiPreset) {
    return "messier_colours.txt"; // beta 2: one design
}

std::string ColourTableText(const SkinColours& colours) {
    std::string text;
    char line[64];
    for(const ColourField& field : kColourFields) {
        const CColor& c = colours.*field.member;
        if(c.alpha == 255)
            std::snprintf(line, sizeof line, "%-14s = #%02X%02X%02X\n", field.name, c.red, c.green, c.blue);
        else
            std::snprintf(line, sizeof line, "%-14s = #%02X%02X%02X%02X\n", field.name, c.red, c.green, c.blue, c.alpha);
        text += line;
    }
    text += std::string("negative       = ") + (colours.negative ? "true" : "false") + "\n";
    return text;
}

int ApplyColourTable(const std::string& text, SkinColours& colours) {
    int taken = 0;
    // C9 (0.2.9): a table saved as "UTF-8 with BOM" keeps its first line.
    const bool bom = text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0;
    std::istringstream lines(bom ? text.substr(3) : text);
    std::string line;
    while(std::getline(lines, line)) {
        const std::size_t comment = line.find(';');
        if(comment != std::string::npos)
            line.erase(comment);
        const std::size_t equals = line.find('=');
        if(equals == std::string::npos)
            continue;
        std::string name = Trim(line.substr(0, equals));
        const std::string value = Trim(line.substr(equals + 1));
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if(name == "negative") {
            if(value == "true" || value == "false") {
                colours.negative = value == "true";
                ++taken;
            }
            continue;
        }
        for(const ColourField& field : kColourFields) {
            CColor colour;
            if(name == field.name && ParseHex(value, colour)) {
                colours.*field.member = colour;
                ++taken;
            }
        }
    }
    return taken;
}

Skin::Skin(GuiPreset preset) : Skin(preset, SkinFolders()) {}

Skin::Skin(GuiPreset preset, const std::vector<std::filesystem::path>& folders)
: preset_(ShownPreset(preset)), base_colours_(PresetInfo(preset_).colours), colours_(base_colours_) {
    const IPlatformFactory& factory = getPlatformFactory();
    // Custom skins (0.2.7): the folders' colour tables, the first folder's last so that
    // it wins, and their bitmaps before the built-in ones.
    // Beta 2: beta 1's table of Slate first, the new one over it. B2C.3 (the user's
    // decision of 2026-11-21): beta 1's table only under Slate's successor, the spiral.
    for(auto folder = folders.rbegin(); folder != folders.rend(); ++folder)
        for(const std::string& table : {std::string("messier_slate_colours.txt"), ColourTableFileName(preset_)}) {
            if(table == "messier_slate_colours.txt" && preset_ != GuiPreset::kSpiral)
                continue;
            std::string text;
            if(ReadSkinTable(*folder / table, text))
                overrides_ += ApplyColourTable(text, base_colours_);
        }
    colours_ = base_colours_;
    // One image, both sizes, under one name: from the folders, else built in. Only a
    // bitmap of the size the brief gives is taken; another one is left out and drawn as the
    // stand-in, so a wrong file cannot distort the layout.
    const auto load = [&](SkinImage& image, const std::string& name1, const std::string& name2, CPoint size) {
        image.size = size;
        PlatformBitmapPtr file1 = BitmapFromFolders(folders, name1, size);
        if(file1) {
            image.x1 = makeOwned<CBitmap>(file1);
            ++overrides_;
        } else if(auto bitmap = factory.createBitmap(CResourceDescription(name1.c_str()))) {
            if(bitmap->getSize() == size)
                image.x1 = makeOwned<CBitmap>(bitmap);
        }
        if(name2.empty())
            return; // MS-R70: an image needed at 1x only
        const CPoint twice(size.x * 2, size.y * 2);
        PlatformBitmapPtr file2 = BitmapFromFolders(folders, name2, twice);
        if(file2) {
            file2->setScaleFactor(2.0);
            image.x2 = makeOwned<CBitmap>(file2);
            ++overrides_;
        } else if(auto bitmap = factory.createBitmap(CResourceDescription(name2.c_str()))) {
            if(bitmap->getSize() == twice) {
                bitmap->setScaleFactor(2.0);
                image.x2 = makeOwned<CBitmap>(bitmap);
            }
        }
    };
    for(std::size_t i = 0; i < kSkinImageCount; ++i) {
        const auto id = static_cast<SkinImageId>(i);
        SkinImage& image = images_[i];
        // MS-R70 (the GUI review of beta 2): the dust masks are soft and drawn from their 1x
        // copy at every zoom (MapView::DrawDustLayer); their 2x files are not decoded.
        const bool dust = id == SkinImageId::kDustWisps || id == SkinImageId::kDustVeil
                          || id == SkinImageId::kDustBands || id == SkinImageId::kDustClouds;
        load(image, SkinImageFileName(preset_, id, 1), dust ? std::string() : SkinImageFileName(preset_, id, 2),
             SkinImageSize(id));
        // Beta 2: beta 1's bitmap where the new one is not there; for one size only where
        // both have the same size (a custom skin with only one of the new files).
        const bool same_size = LegacySkinImageSize(id) == SkinImageSize(id);
        if((!image || (same_size && (!image.x1 || !image.x2))) && !LegacySkinImageFileName(id, 1).empty()) {
            SkinImage old;
            load(old, LegacySkinImageFileName(id, 1), LegacySkinImageFileName(id, 2), LegacySkinImageSize(id));
            if(!image) {
                image = old;
            } else {
                if(!image.x1)
                    image.x1 = old.x1;
                if(!image.x2)
                    image.x2 = old.x2;
            }
        }
    }
    SetAccent(kPresetAccent); // beta 2: the accents in the nebula's hue
}

VSTGUI::CColor AccentColour(double hue) noexcept {
    CColor colour;
    colour.fromHSV(hue, 0.76, 0.87);
    return colour;
}

void Skin::SetAccent(std::int32_t hue) noexcept {
    const SkinColours& original = base_colours_;
    colours_ = original;
    // 0.2.6 (the user's wish): only the hue turns. Each colour keeps its own lightness and
    // saturation, so contrast and brightness stay as the design made them. Beta 2 (the
    // user's wishes of 2026-11-20): HUE, from red over violet to blue, colours the nebula
    // and the accents (START's fill, the knobs' arcs, lit switches); the labels stay neutral.
    // Without a HUE of its own the accents take the nebula's hue. Round 5 (the user's wish):
    // the value texts follow too, at their own lightness, so they stay as readable.
    const auto turned = [](CColor colour, double to) {
        double h = 0, saturation = 0, lightness = 0;
        colour.toHSL(h, saturation, lightness);
        CColor result = colour;
        result.fromHSL(to, saturation, lightness);
        result.alpha = colour.alpha;
        return result;
    };
    double to = 0;
    if(hue == kPresetAccent || !IsAccent(hue)) {
        double saturation = 0, lightness = 0;
        original.nebula_a.toHSL(to, saturation, lightness);
    } else {
        to = AccentOnScale(hue);
        colours_.nebula_a = turned(original.nebula_a, to);
        colours_.nebula_b = turned(original.nebula_b, to);
    }
    colours_.start_fill = turned(original.start_fill, to);
    colours_.control = turned(original.control, to);
    colours_.value = turned(original.value, to);
    colours_.plate_wave = turned(original.plate_wave, to); // the user's DAW test of beta 2
}

CFontRef Skin::FontOf(int kind, double size) const {
    for(const Font& font : fonts_)
        if(font.kind == kind && font.size == size)
            return font.font;
    // System fonts of Windows: Palatino Linotype for the brand, the version and the
    // italic file name, Bahnschrift for the controls' texts (0.2.1), Consolas for values
    // (the study's web fonts are not in the bundle); Bahnschrift in its regular weight
    // since 0.2.2 (VSTGUI offers regular or bold). Never dropped while the skin lives, so
    // a font handed out stays valid.
    Font font{kind, size, nullptr};
    switch(kind) {
        case 0: font.font = makeOwned<CFontDesc>("Palatino Linotype", size, kBoldFace); break;
        case 1: font.font = makeOwned<CFontDesc>("Palatino Linotype", size, kItalicFace); break;
        // Beta 2 (the user's wish): thinner, the semi-light cut of Bahnschrift (Windows 10
        // and 11), for the values too (its figures are of one width; Cascadia Mono's light
        // cuts are not offered as families).
        case 3: font.font = makeOwned<CFontDesc>("Bahnschrift SemiLight", size); break;
        default: font.font = makeOwned<CFontDesc>("Bahnschrift SemiLight", size); break;
    }
    fonts_.push_back(font);
    return font.font;
}

CFontRef Skin::Caps(double size) const {
    return FontOf(0, size);
}

CFontRef Skin::Sans(double size) const {
    return FontOf(3, size);
}

CFontRef Skin::Italic(double size) const {
    return FontOf(1, size);
}

CFontRef Skin::Mono(double size) const {
    return FontOf(2, size);
}

void DrawSkinImagePart(CDrawContext* context, const SkinImage& image, const CRect& source, const CRect& dest,
                       float alpha) {
    CBitmap* bitmap = Pick(context, image);
    if(!bitmap || source.getWidth() <= 0 || source.getHeight() <= 0 || dest.getWidth() <= 0
       || dest.getHeight() <= 0)
        return;
    // The bitmap's own units (equal to the image's for a correct file).
    const CPoint have = bitmap->getSize();
    const double kx = image.size.x > 0 ? have.x / image.size.x : 1.0;
    const double ky = image.size.y > 0 ? have.y / image.size.y : 1.0;
    const CRect from(source.left * kx, source.top * ky, source.right * kx, source.bottom * ky);
    const double sx = dest.getWidth() / from.getWidth();
    const double sy = dest.getHeight() / from.getHeight();
    const CGraphicsTransform map(sx, 0.0, 0.0, sy, dest.left - sx * from.left, dest.top - sy * from.top);
    context->setBitmapInterpolationQuality(BitmapInterpolationQuality::kHigh);
    CDrawContext::Transform transform(*context, map);
    context->drawBitmap(bitmap, from, from.getTopLeft(), alpha);
}

void DrawSkinImage(CDrawContext* context, const SkinImage& image, const CRect& dest, float alpha) {
    DrawSkinImagePart(context, image, CRect(0, 0, image.size.x, image.size.y), dest, alpha);
}

void DrawNinePart(CDrawContext* context, const SkinImage& image, const CRect& dest, CCoord edge, float alpha) {
    if(!image)
        return;
    const CCoord w = image.size.x, h = image.size.y;
    const CCoord sx[4] = {0, edge, w - edge, w};
    const CCoord sy[4] = {0, edge, h - edge, h};
    const CCoord dx[4] = {SnapX(context, dest.left), SnapX(context, dest.left + edge),
                          SnapX(context, dest.right - edge), SnapX(context, dest.right)};
    const CCoord dy[4] = {SnapY(context, dest.top), SnapY(context, dest.top + edge),
                          SnapY(context, dest.bottom - edge), SnapY(context, dest.bottom)};
    for(int row = 0; row < 3; ++row)
        for(int column = 0; column < 3; ++column)
            DrawSkinImagePart(context, image, CRect(sx[column], sy[row], sx[column + 1], sy[row + 1]),
                              CRect(dx[column], dy[row], dx[column + 1], dy[row + 1]), alpha);
}

CCoord TrackedWidth(CDrawContext* context, const std::string& text, CCoord tracking, CFontRef font) {
    context->setFont(font);
    CCoord width = 0;
    const std::vector<std::string> characters = Characters(text);
    for(const std::string& c : characters)
        width += context->getStringWidth(c.c_str());
    if(characters.size() > 1)
        width += tracking * static_cast<CCoord>(characters.size() - 1);
    return width;
}

void DrawTracked(CDrawContext* context, const std::string& text, const CRect& box, CHoriTxtAlign align,
                 CCoord tracking, CFontRef font, CColor colour, const SkinColours* engraved) {
    if(text.empty())
        return;
    const CCoord width = TrackedWidth(context, text, tracking, font);
    CCoord x = box.left;
    if(align == kCenterText)
        x = box.left + (box.getWidth() - width) / 2;
    else if(align == kRightText)
        x = box.right - width;
    context->setFont(font);
    const std::vector<std::string> characters = Characters(text);
    struct Copy {
        CCoord dy;
        CColor colour;
    };
    Copy copies[3] = {{-1, {}}, {1, {}}, {0, colour}};
    int first = 2;
    if(engraved) {
        copies[0].colour = engraved->engrave_dark;
        copies[1].colour = engraved->engrave_light;
        first = 0;
    }
    for(int k = first; k < 3; ++k) {
        context->setFontColor(copies[k].colour);
        CCoord at = x;
        for(const std::string& c : characters) {
            const CCoord w = context->getStringWidth(c.c_str());
            context->drawString(c.c_str(), CRect(at, box.top + copies[k].dy, at + w + 4, box.bottom + copies[k].dy),
                                kLeftText);
            at += w + tracking;
        }
    }
}

void DrawRule(CDrawContext* context, const SkinColours& colours, CPoint from, CPoint to) {
    context->setLineWidth(1);
    context->setLineStyle(kLineSolid);
    const bool vertical = from.x == to.x;
    const CPoint step = vertical ? CPoint(1, 0) : CPoint(0, 1);
    context->setFrameColor(colours.rule_dark);
    context->drawLine(from + CPoint(step.x * 0.5, step.y * 0.5), to + CPoint(step.x * 0.5, step.y * 0.5));
    context->setFrameColor(colours.rule_light);
    context->drawLine(from + CPoint(step.x * 1.5, step.y * 1.5), to + CPoint(step.x * 1.5, step.y * 1.5));
}

CColor WithAlpha(CColor colour, double alpha) {
    colour.alpha = static_cast<uint8_t>(std::clamp(colour.alpha * alpha, 0.0, 255.0) + 0.5);
    return colour;
}

} // namespace messier
