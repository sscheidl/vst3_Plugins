#include "gui_skin.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/cresourcedescription.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include "vstgui/lib/platform/platformfactory.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

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

// The colours of the four presets (0.2.4): the delivered bitmap package of 2026-11-19
// (its FARBTABELLEN.md), all delivered. Slate is the graphite standard of the GUI
// template; the order is that of the stored preset IDs.
const GuiPresetInfo kPresets[kGuiPresetCount] = {
    {"Slate", "slate", true,
     {
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
     }},

    {"Silver", "silver", true,
     {
         .metal = Rgb(185, 191, 195),
         .stain = Rgb(137, 148, 158),
         .glass = Rgb(18, 27, 43),
         .text = Rgb(34, 41, 50),
         .dim = Rgb(58, 70, 84),
         .marker = Rgb(187, 83, 47),
         .engrave_light = Rgba(255, 255, 255, 0.58039216),
         .engrave_dark = Rgba(49, 58, 68, 0.14509804),
         .rule_dark = Rgba(37, 50, 64, 0.37647059),
         .rule_light = Rgba(255, 255, 255, 0.51764706),
         .grid_line = Rgba(187, 205, 238, 0.10196078),
         .grid_cross = Rgba(173, 194, 224, 0.38823529),
         .plate_text = Rgb(225, 233, 242),
         .nebula_a = Rgb(88, 110, 172),
         .nebula_b = Rgb(50, 59, 115),
         .grain = Rgb(251, 248, 238),
         .tail = Rgb(232, 190, 144),
         .playhead = Rgb(246, 246, 242),
         .loop_fill = Rgba(157, 186, 229, 0.07450980),
         .spray_fill = Rgba(181, 200, 235, 0.09803922),
         .loop_edge = Rgba(233, 163, 122, 0.47843137),
         .wave = Rgb(199, 217, 243),
         .wave_outside = Rgb(126, 157, 189),
         .plate_wave = Rgb(170, 195, 232),
         .groove = Rgba(42, 55, 66, 0.65882353),
         .tick = Rgba(52, 66, 76, 0.78039216),
         .pointer = Rgb(37, 45, 53),
         .button_text = Rgb(35, 44, 56),
         .warning = Rgb(127, 41, 31),
         .negative = false,
         .control = Rgb(52, 77, 131),
         .value = Rgb(38, 56, 102),
     }},

    {"Copper", "copper", true,
     {
         .metal = Rgb(58, 41, 35),
         .stain = Rgb(115, 71, 51),
         .glass = Rgb(22, 16, 25),
         .text = Rgb(243, 219, 202),
         .dim = Rgb(211, 177, 158),
         .marker = Rgb(242, 142, 85),
         .engrave_light = Rgba(255, 233, 213, 0.10588235),
         .engrave_dark = Rgba(17, 8, 5, 0.73725490),
         .rule_dark = Rgba(25, 14, 12, 0.68627451),
         .rule_light = Rgba(216, 158, 116, 0.18431373),
         .grid_line = Rgba(203, 169, 194, 0.08235294),
         .grid_cross = Rgba(216, 174, 198, 0.36078431),
         .plate_text = Rgb(241, 218, 218),
         .nebula_a = Rgb(159, 74, 96),
         .nebula_b = Rgb(98, 47, 81),
         .grain = Rgb(255, 242, 221),
         .tail = Rgb(239, 181, 135),
         .playhead = Rgb(255, 240, 228),
         .loop_fill = Rgba(210, 162, 152, 0.08627451),
         .spray_fill = Rgba(217, 167, 146, 0.10196078),
         .loop_edge = Rgba(242, 142, 85, 0.49019608),
         .wave = Rgb(242, 185, 180),
         .wave_outside = Rgb(168, 111, 125),
         .plate_wave = Rgb(218, 165, 178),
         .groove = Rgba(16, 12, 10, 0.74901961),
         .tick = Rgba(229, 191, 171, 0.69411765),
         .pointer = Rgb(41, 26, 23),
         .button_text = Rgb(246, 223, 208),
         .warning = Rgb(255, 170, 132),
         .negative = false,
         .control = Rgb(161, 207, 189),
         .value = Rgb(195, 230, 214),
     }},

    {"Photo plate", "plate", true,
     {
         .metal = Rgb(48, 41, 31),
         .stain = Rgb(88, 98, 80),
         .glass = Rgb(224, 226, 213),
         .text = Rgb(243, 231, 205),
         .dim = Rgb(211, 194, 162),
         .marker = Rgb(196, 80, 43),
         .engrave_light = Rgba(255, 245, 219, 0.13725490),
         .engrave_dark = Rgba(24, 17, 10, 0.73725490),
         .rule_dark = Rgba(24, 18, 10, 0.72156863),
         .rule_light = Rgba(201, 177, 125, 0.25882353),
         .grid_line = Rgba(55, 63, 61, 0.10980392),
         .grid_cross = Rgba(55, 65, 61, 0.47058824),
         .plate_text = Rgb(46, 58, 57),
         .nebula_a = Rgb(124, 130, 118),
         .nebula_b = Rgb(161, 148, 128),
         .grain = Rgb(44, 39, 49),
         .tail = Rgb(86, 67, 83),
         .playhead = Rgb(39, 47, 49),
         .loop_fill = Rgba(74, 83, 69, 0.10980392),
         .spray_fill = Rgba(103, 102, 75, 0.15294118),
         .loop_edge = Rgba(169, 72, 48, 0.62745098),
         .wave = Rgb(64, 72, 89),
         .wave_outside = Rgb(102, 108, 115),
         .plate_wave = Rgb(63, 74, 86),
         .groove = Rgba(16, 14, 9, 0.76862745),
         .tick = Rgba(225, 197, 150, 0.70196078),
         .pointer = Rgb(45, 34, 19),
         .button_text = Rgb(246, 232, 201),
         .warning = Rgb(255, 183, 119),
         .negative = true,
         .control = Rgb(220, 197, 139),
         .value = Rgb(243, 223, 178),
     }},
};

struct ImageSpec {
    const char* stem;
    bool per_preset;
    int width, height;
};

// docs/GRAFIKAUFTRAG_GUI.md; the keys as delivered (keys_white, keys_black).
constexpr ImageSpec kImages[kSkinImageCount] = {
    {"chassis", true, 880, 724}, {"bezel", true, 64, 64},      {"plate", true, 840, 186},
    {"nebula_a", false, 1000, 260}, {"nebula_b", false, 1000, 260}, {"knob_l", true, 72, 72},
    {"knob_s", true, 48, 48},    {"thumb", true, 16, 28},      {"button", true, 48, 28},
    {"button_down", true, 48, 28}, {"keys_white", false, 36, 66},
    {"keys_black", false, 22, 40},
};

// The bitmap of `image` that suits the context: the 2x one above 100 % device pixels.
CBitmap* Pick(CDrawContext* context, const SkinImage& image) {
    const CGraphicsTransform t = context->getCurrentTransform();
    const double scale = context->getScaleFactor() * std::abs(t.m11);
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

// The white nebula in the preset's tint, its alpha kept.
void Tint(CBitmap* bitmap, CColor tint) {
    if(!bitmap)
        return;
    auto access = owned(CBitmapPixelAccess::create(bitmap, false));
    if(!access)
        return;
    CColor c;
    do {
        access->getColor(c);
        c.red = tint.red;
        c.green = tint.green;
        c.blue = tint.blue;
        access->setColor(c);
    } while(++(*access));
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
    return IsGuiPreset(index) && kPresets[index].delivered ? preset : GuiPreset::kSlate;
}

std::string SkinImageFileName(GuiPreset preset, SkinImageId id, int scale) {
    const ImageSpec& spec = kImages[static_cast<std::size_t>(id)];
    std::string name = "messier_";
    if(spec.per_preset) {
        name += PresetInfo(preset).file_key;
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

Skin::Skin(GuiPreset preset) : preset_(ShownPreset(preset)), colours_(PresetInfo(preset_).colours) {
    const IPlatformFactory& factory = getPlatformFactory();
    for(std::size_t i = 0; i < kSkinImageCount; ++i) {
        const auto id = static_cast<SkinImageId>(i);
        SkinImage& image = images_[i];
        image.size = SkinImageSize(id);
        // Only a bitmap of the size the brief gives is taken; another one is left out
        // and drawn as the stand-in, so a wrong file cannot distort the layout.
        const std::string name1 = SkinImageFileName(preset_, id, 1);
        if(auto bitmap = factory.createBitmap(CResourceDescription(name1.c_str()))) {
            if(bitmap->getSize() == CPoint(image.size.x, image.size.y))
                image.x1 = makeOwned<CBitmap>(bitmap);
        }
        const std::string name2 = SkinImageFileName(preset_, id, 2);
        if(auto bitmap = factory.createBitmap(CResourceDescription(name2.c_str()))) {
            if(bitmap->getSize() == CPoint(image.size.x * 2, image.size.y * 2)) {
                bitmap->setScaleFactor(2.0);
                image.x2 = makeOwned<CBitmap>(bitmap);
            }
        }
    }
    for(auto [id, tint] : {std::pair{SkinImageId::kNebulaA, colours_.nebula_a},
                           std::pair{SkinImageId::kNebulaB, colours_.nebula_b}}) {
        SkinImage& image = images_[static_cast<std::size_t>(id)];
        Tint(image.x1, tint);
        Tint(image.x2, tint);
    }
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
        case 3: font.font = makeOwned<CFontDesc>("Bahnschrift", size); break;
        default: font.font = makeOwned<CFontDesc>("Consolas", size); break;
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
