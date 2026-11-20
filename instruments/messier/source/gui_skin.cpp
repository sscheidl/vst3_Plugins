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

// The colours of the four presets: the delivered packages of 2026-11-19 (their
// FARBTABELLEN.md). Slate is the graphite standard of the GUI template; since 0.2.5 Plum
// and Gold take the places (and stored IDs) of Copper and Photo plate.
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

    {"Plum", "plum", true,
     {
         .metal = Rgb(237, 232, 237),
         .stain = Rgb(226, 220, 229),
         .glass = Rgb(224, 226, 213),
         .text = Rgb(43, 28, 47),
         .dim = Rgb(82, 69, 86),
         .marker = Rgb(196, 80, 43),
         .engrave_light = Rgba(255, 255, 255, 0.49019608),
         .engrave_dark = Rgba(81, 64, 94, 0.12549020),
         .rule_dark = Rgba(67, 44, 81, 0.26274510),
         .rule_light = Rgba(255, 255, 255, 0.50980392),
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
         .groove = Rgba(69, 50, 74, 0.37647059),
         .tick = Rgba(93, 76, 96, 0.67058824),
         .pointer = Rgb(42, 34, 46),
         .button_text = Rgb(46, 32, 51),
         .warning = Rgb(145, 46, 53),
         .negative = true,
         .control = Rgb(101, 54, 83),
         .value = Rgb(80, 39, 68),
     }},

    {"Gold", "gold", true,
     {
         .metal = Rgb(239, 222, 185),
         .stain = Rgb(219, 199, 160),
         .glass = Rgb(7, 24, 14),
         .text = Rgb(25, 40, 27),
         .dim = Rgb(56, 75, 51),
         .marker = Rgb(213, 106, 34),
         .engrave_light = Rgba(255, 246, 222, 0.54901961),
         .engrave_dark = Rgba(71, 54, 23, 0.14901961),
         .rule_dark = Rgba(89, 75, 43, 0.34901961),
         .rule_light = Rgba(255, 242, 207, 0.52941176),
         .grid_line = Rgba(109, 175, 87, 0.20000000),
         .grid_cross = Rgba(154, 205, 118, 0.56078431),
         .plate_text = Rgb(184, 223, 155),
         .nebula_a = Rgb(38, 74, 43),
         .nebula_b = Rgb(32, 57, 35),
         .grain = Rgb(255, 244, 220),
         .tail = Rgb(229, 215, 158),
         .playhead = Rgb(235, 242, 207),
         .loop_fill = Rgba(102, 157, 57, 0.08235294),
         .spray_fill = Rgba(140, 182, 96, 0.09411765),
         .loop_edge = Rgba(247, 148, 82, 0.57254902),
         .wave = Rgb(115, 169, 94),
         .wave_outside = Rgb(76, 121, 71),
         .plate_wave = Rgb(103, 164, 81),
         .groove = Rgba(56, 71, 50, 0.62745098),
         .tick = Rgba(74, 101, 76, 0.65882353),
         .pointer = Rgb(32, 56, 35),
         .button_text = Rgb(30, 53, 34),
         .warning = Rgb(142, 55, 36),
         .negative = false,
         .control = Rgb(52, 91, 56),
         .value = Rgb(44, 77, 45),
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
    {"value", &SkinColours::value},
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
            if(bitmap && bitmap->getSize() == size)
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
        "Bitmaps: PNG, the size below for @1x and exactly twice that for @2x.\n"
        "<preset> is slate, silver, plum or gold.\n\n";
    char line[128];
    for(const ImageSpec& spec : kImages) {
        const std::string name = std::string("messier_") + (spec.per_preset ? "<preset>_" : "") + spec.stem + "@1x.png";
        std::snprintf(line, sizeof line, "  %-36s %4d x %-4d   @2x.png: %d x %d\n", name.c_str(), spec.width,
                      spec.height, spec.width * 2, spec.height * 2);
        text += line;
    }
    text += "\nThe two nebula bitmaps are white; the colours nebula_a and nebula_b tint them.\n\n"
            "Colour tables: messier_<preset>_colours.txt, one line per colour,\n"
            "  name = #RRGGBB or #RRGGBBAA (AA: opacity); from \";\" on a comment.\n"
            "A line left out keeps the built-in colour. The tables of both folders apply;\n"
            "where both set a colour, the one in Documents wins. Names:\n";
    for(const ColourField& field : kColourFields) {
        text += "  ";
        text += field.name;
        text += "\n";
    }
    text += "  negative = true or false (dark ink on bright glass)\n";
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

std::string ColourTableFileName(GuiPreset preset) {
    return std::string("messier_") + PresetInfo(preset).file_key + "_colours.txt";
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
    std::istringstream lines(text);
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
    for(auto folder = folders.rbegin(); folder != folders.rend(); ++folder) {
        std::string text;
        if(ReadSkinTable(*folder / ColourTableFileName(preset_), text))
            overrides_ += ApplyColourTable(text, base_colours_);
    }
    colours_ = base_colours_;
    for(std::size_t i = 0; i < kSkinImageCount; ++i) {
        const auto id = static_cast<SkinImageId>(i);
        SkinImage& image = images_[i];
        image.size = SkinImageSize(id);
        // Only a bitmap of the size the brief gives is taken; another one is left out
        // and drawn as the stand-in, so a wrong file cannot distort the layout.
        const std::string name1 = SkinImageFileName(preset_, id, 1);
        PlatformBitmapPtr file1 = BitmapFromFolders(folders, name1, CPoint(image.size.x, image.size.y));
        if(file1) {
            image.x1 = makeOwned<CBitmap>(file1);
            ++overrides_;
        } else if(auto bitmap = factory.createBitmap(CResourceDescription(name1.c_str()))) {
            if(bitmap->getSize() == CPoint(image.size.x, image.size.y))
                image.x1 = makeOwned<CBitmap>(bitmap);
        }
        const std::string name2 = SkinImageFileName(preset_, id, 2);
        PlatformBitmapPtr file2 = BitmapFromFolders(folders, name2, CPoint(image.size.x * 2, image.size.y * 2));
        if(file2) {
            file2->setScaleFactor(2.0);
            image.x2 = makeOwned<CBitmap>(file2);
            ++overrides_;
        } else if(auto bitmap = factory.createBitmap(CResourceDescription(name2.c_str()))) {
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

VSTGUI::CColor AccentColour(double hue) noexcept {
    CColor colour;
    colour.fromHSV(hue, 0.76, 0.87);
    return colour;
}

void Skin::SetAccent(std::int32_t hue) noexcept {
    const SkinColours& original = base_colours_;
    colours_ = original;
    if(hue == kPresetAccent || !IsAccent(hue))
        return;
    // 0.2.6 (the user's wish): only the hue turns. Each colour keeps its own lightness and
    // saturation, so contrast and brightness stay as the preset made them; near-grey ink
    // (most labels) hardly changes, the coloured controls and values take the new hue.
    const auto turned = [&](CColor colour) {
        double h = 0, saturation = 0, lightness = 0;
        colour.toHSL(h, saturation, lightness);
        CColor result = colour;
        result.fromHSL(static_cast<double>(hue), saturation, lightness);
        result.alpha = colour.alpha;
        return result;
    };
    colours_.control = turned(original.control);
    colours_.value = turned(original.value);
    colours_.text = turned(original.text);
    colours_.button_text = turned(original.button_text);
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
