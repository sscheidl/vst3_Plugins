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

// The colours of the window study, version 3, and of docs/GRAFIKAUFTRAG_GUI.md.
const GuiPresetInfo kPresets[kGuiPresetCount] = {
    {"Slate", "slate", true,
     {Rgb(46, 50, 53), Rgb(62, 74, 82), Rgb(11, 15, 26), Rgb(220, 214, 202), Rgb(142, 137, 127),
      Rgb(227, 115, 61), Rgba(255, 255, 255, 0.07), Rgba(0, 0, 0, 0.75), Rgba(0, 0, 0, 0.6),
      Rgba(255, 255, 255, 0.07), Rgba(175, 190, 215, 0.07), Rgba(175, 190, 215, 0.28),
      Rgba(220, 226, 236, 0.75), Rgb(104, 82, 156), Rgb(56, 68, 128), Rgb(255, 238, 210),
      Rgb(255, 214, 170), Rgb(242, 230, 214), Rgba(205, 215, 240, 0.06), Rgba(205, 215, 240, 0.07),
      Rgba(220, 226, 236, 0.35), Rgb(242, 220, 188), Rgb(140, 146, 150), Rgb(176, 188, 222), Rgba(0, 0, 0, 0.6),
      Rgba(220, 214, 200, 0.6), Rgb(18, 20, 21), Rgb(220, 214, 202), Rgb(236, 172, 64), false}},
    {"Silver", "silver", false,
     {Rgb(176, 178, 177), Rgb(150, 154, 153), Rgb(11, 15, 26), Rgb(28, 31, 33), Rgb(75, 79, 81),
      Rgb(201, 83, 28), Rgba(255, 255, 255, 0.55), Rgba(0, 0, 0, 0.18), Rgba(0, 0, 0, 0.3),
      Rgba(255, 255, 255, 0.55), Rgba(175, 190, 215, 0.07), Rgba(175, 190, 215, 0.28),
      Rgba(220, 226, 236, 0.75), Rgb(104, 82, 156), Rgb(56, 68, 128), Rgb(255, 238, 210),
      Rgb(255, 214, 170), Rgb(242, 230, 214), Rgba(205, 215, 240, 0.06), Rgba(205, 215, 240, 0.07),
      Rgba(220, 226, 236, 0.35), Rgb(250, 236, 214), Rgb(92, 96, 98), Rgb(176, 188, 222), Rgba(0, 0, 0, 0.35),
      Rgba(30, 34, 36, 0.6), Rgb(18, 20, 21), Rgb(28, 31, 33), Rgb(176, 96, 20), false}},
    {"Copper", "copper", false,
     {Rgb(56, 40, 34), Rgb(88, 58, 44), Rgb(15, 9, 9), Rgb(228, 199, 173), Rgb(158, 126, 105),
      Rgb(241, 124, 62), Rgba(255, 255, 255, 0.07), Rgba(0, 0, 0, 0.75), Rgba(0, 0, 0, 0.6),
      Rgba(255, 255, 255, 0.07), Rgba(220, 150, 120, 0.07), Rgba(220, 150, 120, 0.24),
      Rgba(236, 210, 190, 0.75), Rgb(138, 52, 66), Rgb(96, 42, 66), Rgb(255, 196, 140),
      Rgb(244, 146, 86), Rgb(241, 124, 62), Rgba(240, 150, 110, 0.06), Rgba(240, 150, 110, 0.07),
      Rgba(236, 210, 190, 0.35), Rgb(244, 178, 128), Rgb(130, 96, 80), Rgb(226, 170, 150), Rgba(0, 0, 0, 0.6),
      Rgba(228, 199, 173, 0.55), Rgb(18, 12, 10), Rgb(228, 199, 173), Rgb(236, 172, 64), false}},
    {"Photo plate", "plate", false,
     {Rgb(58, 52, 42), Rgb(64, 96, 79), Rgb(205, 209, 202), Rgb(226, 217, 198), Rgb(164, 154, 134),
      Rgb(208, 168, 92), Rgba(255, 255, 255, 0.07), Rgba(0, 0, 0, 0.75), Rgba(0, 0, 0, 0.6),
      Rgba(255, 255, 255, 0.07), Rgba(25, 28, 26, 0.12), Rgba(25, 28, 26, 0.42), Rgba(20, 22, 21, 0.75),
      Rgb(120, 116, 108), Rgb(150, 142, 128), Rgb(20, 22, 21), Rgb(30, 32, 31), Rgb(29, 32, 31),
      Rgba(20, 22, 21, 0.04), Rgba(20, 22, 21, 0.08), Rgba(20, 22, 21, 0.45), Rgb(232, 222, 200),
      Rgb(130, 124, 110), Rgb(40, 44, 42), Rgba(0, 0, 0, 0.6), Rgba(226, 217, 198, 0.6), Rgb(20, 17, 12), Rgb(226, 217, 198),
      Rgb(236, 172, 64), true}},
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
    {"button_down", true, 48, 28}, {"screw", true, 14, 14},    {"keys_white", false, 36, 66},
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
    // (the study's web fonts are not in the bundle). Never dropped while the skin lives,
    // so a font handed out stays valid.
    Font font{kind, size, nullptr};
    switch(kind) {
        case 0: font.font = makeOwned<CFontDesc>("Palatino Linotype", size, kBoldFace); break;
        case 1: font.font = makeOwned<CFontDesc>("Palatino Linotype", size, kItalicFace); break;
        case 3: font.font = makeOwned<CFontDesc>("Bahnschrift", size, kBoldFace); break;
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
