// Draws the plug-in window as it is into PNG files, without a host: for design rounds
// (0.2.2), so the user and Codex can see the implemented window at 1x and 2x, with the
// keyboard shown and hidden, at both octave limits, with keys on and off. Run by hand,
// not a test:
//
//     preview_messier_window <folder>
//
// The window is the real Editor, opened in a hidden window, with the bundle's bitmaps
// (resource/gui) and a made-up file of 24 s: a waveform, a loop, the playhead on a
// labelled second and a few grains. Each picture is the frame drawn into an offscreen
// bitmap at the device scale named in the file name.
#include "controller.h"
#include "editor.h"
#include "file_loader.h"
#include "map_view.h"
#include "parameters.h"
#include "view_layout.h"
#include "widgets.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/win32/win32factory.h"
#include "vstgui/lib/vstguiinit.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace messier;

// The SDK's module code asks for it; this program is no module.
void* moduleHandle = nullptr;

namespace {

constexpr std::uint32_t kRate = 48000;
constexpr std::uint32_t kFrames = 24 * kRate;

std::shared_ptr<WaveOverview> MakeWave() {
    auto wave = std::make_shared<WaveOverview>();
    wave->sequence = 1;
    wave->frames = kFrames;
    wave->columns = WaveOverview::kMaxColumns;
    wave->rate = kRate;
    std::uint32_t seed = 12345;
    for(std::uint32_t c = 0; c < wave->columns; ++c) {
        const double t = 24.0 * c / wave->columns;
        // Phrases of a few seconds with attacks, a slow swell, some noise.
        const double phrase = std::fmod(t, 3.1) / 3.1;
        double env = 0.25 + 0.55 * std::exp(-phrase * 2.5) + 0.15 * std::sin(t * 0.7) * std::sin(t * 0.7);
        seed = seed * 1664525u + 1013904223u;
        env *= 0.75 + 0.25 * ((seed >> 8) / 16777216.0);
        env = std::min(env, 0.98);
        wave->high[c] = static_cast<std::int16_t>(env * 32767);
        seed = seed * 1664525u + 1013904223u;
        wave->low[c] = static_cast<std::int16_t>(-env * (0.85 + 0.15 * ((seed >> 8) / 16777216.0)) * 32767);
        // Round 4: a body of about a third of the peaks, more in the louder phrases.
        seed = seed * 1664525u + 1013904223u;
        wave->rms[c] = static_cast<std::uint16_t>(env * env * (0.35 + 0.2 * ((seed >> 8) / 16777216.0)) * 32767);
    }
    return wave;
}

EngineDisplay MakeDisplay(bool frozen, bool dense = false) {
    EngineDisplay d;
    d.sequence = 1;
    d.frames = kFrames;
    // The loop as the core sets it from START 0.36 and SIZE 0.75 (main()).
    d.loop_start = 0.36 * kFrames;
    d.loop_length = 0.75 * 0.75 * (kFrames - d.loop_start);
    d.playhead = 14.0 * kRate; // on the axis' label "14 s"
    d.spray = static_cast<float>(0.2 * kRate);
    d.frozen = frozen ? 1 : 0;
    d.head_speed = frozen ? 0.0f : static_cast<float>(0.89 * kRate);
    d.rendered = 1;
    const GrainDot grains[] = {
        {static_cast<float>(14.4 * kRate), 0.12f, 0.45f, 48000.0f, 2.5f},
        {static_cast<float>(13.1 * kRate), 0.30f, 0.70f, 36000.0f, 3.0f},
        {static_cast<float>(15.3 * kRate), 0.62f, 0.35f, 60000.0f, 2.0f},
        {static_cast<float>(12.2 * kRate), 0.80f, 0.55f, -24000.0f, 2.2f},
        {static_cast<float>(16.0 * kRate), 0.90f, 0.20f, 48000.0f, 4.0f},
    };
    d.grain_count = static_cast<std::uint32_t>(std::size(grains));
    for(std::uint32_t i = 0; i < d.grain_count; ++i)
        d.grains[i] = grains[i];
    if(dense) { // 0.2.3: many grains at once, for the rays at high density
        d.grain_count = EngineDisplay::kMaxGrains;
        for(std::uint32_t i = 0; i < d.grain_count; ++i) {
            const double k = (i * 0.618034) - std::floor(i * 0.618034);
            d.grains[i] = {static_cast<float>((11.0 + 6.0 * k) * kRate), static_cast<float>((i * 0.37) - std::floor(i * 0.37)),
                           static_cast<float>(0.15 + 0.7 * k), 48000.0f, 2.5f};
        }
    }
    return d;
}

bool Render(Editor& editor, double scale, const std::filesystem::path& file) {
    VSTGUI::CFrame* frame = editor.getFrame();
    if(!frame)
        return false;
    const VSTGUI::CRect area = frame->getViewSize();
    // Above 1x a bitmap of more device pixels; below, the window zoomed down as a host's
    // window at that size would draw it.
    const double device = std::max(scale, 1.0);
    const double zoom = std::min(scale, 1.0);
    auto context = VSTGUI::COffscreenContext::create(
        VSTGUI::CPoint(std::round(area.getWidth() * zoom), std::round(area.getHeight() * zoom)), device);
    if(!context)
        return false;
    context->beginDraw();
    {
        VSTGUI::CDrawContext::Transform transform(*context, VSTGUI::CGraphicsTransform().scale(zoom, zoom));
        frame->drawRect(context, area);
    }
    context->endDraw();
    VSTGUI::CBitmap* bitmap = context->getBitmap();
    if(!bitmap || !bitmap->getPlatformBitmap())
        return false;
    const auto png = VSTGUI::getPlatformFactory().createBitmapMemoryPNGRepresentation(bitmap->getPlatformBitmap());
    if(png.empty())
        return false;
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    std::printf("%s\n", file.filename().string().c_str());
    return static_cast<bool>(out);
}

struct Case {
    const char* name;
    bool keyboard;
    int transpose;
    bool latch, stop, freeze;
    double pitch = 0;   // octaves (0.2.3: the grains' tint)
    bool dense = false; // many grains
    GuiPreset preset = GuiPreset::kSpiral; // 0.2.4
    bool details = false;                 // 0.2.4: DETAILS open
    std::int32_t hue = kPresetAccent;     // 0.2.6: HUE (the preset's own colour)
    std::int32_t wave = kPresetWave;
    const char* file = "down from.wav"; // 0.2.7: a long name too
    bool galaxy = true;                 // beta 2: GALAXY or WAVE in the glass
    MapView::Dust dust = MapView::Dust::kWisps; // beta 2: the dust experiment
    double dust_strength = 0.5;
    // Round 4: DETAILS in the info line (else in the START row); round 5: neither.
    double blend = 0.0; // round 5: BLEND, which thickens the dark clouds
    double turn = 0.0;  // round 7: the waveform turned by this share of the file
};

} // namespace

int main(int argc, char** argv) {
    // 0.2.7: the built-in skin, whatever custom skin this computer holds; round 8: or with
    // the skin folder named third (to try delivered bitmaps in the real window).
    if(argc >= 3 && std::string(argv[1]) != "--export-skin")
        UseSkinFolders({std::filesystem::path(argv[2])});
    else
        UseSkinFolders({});
    if(argc < 2) {
        std::fprintf(stderr, "usage: preview_messier_window <folder> [<skin folder>] | --export-skin <folder>\n");
        return 2;
    }
    // 0.2.6: the four presets' colour tables as text, the start of a skin folder.
    if(std::string(argv[1]) == "--export-skin" && argc >= 3) {
        const std::filesystem::path skin = argv[2];
        std::filesystem::create_directories(skin);
        for(std::int32_t p = 0; p < kGuiPresetCount; ++p) {
            const auto preset = static_cast<GuiPreset>(p);
            std::ofstream out(skin / ColourTableFileName(preset), std::ios::binary);
            out << "; Messier skin colours: " << PresetInfo(preset).name << "\n";
            out << "; name = #RRGGBB or #RRGGBBAA; delete a line to keep the built-in value\n";
            out << ColourTableText(PresetInfo(preset).colours);
            std::printf("%s\n", ColourTableFileName(preset).c_str());
        }
        return 0;
    }
    const std::filesystem::path folder = argv[1];
    std::filesystem::create_directories(folder);
    // A host has COM running before it opens a window; the bitmaps' decoder needs it.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    VSTGUI::init(GetModuleHandleW(nullptr));
    if(auto* win32 = VSTGUI::getPlatformFactory().asWin32Factory())
        win32->setResourceBasePath(VSTGUI::UTF8String(MESSIER_GUI_DIR "/"));
    HWND parent = CreateWindowExW(0, L"STATIC", L"Messier preview", WS_POPUP, 0, 0, 900, 900, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    int failures = parent ? 0 : 1;
    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Controller> controller = owned(new Controller());
    controller->initialize(app);
    // Values near the draft's.
    for(const auto& [id, value] : {std::pair{kParamStart, 0.36}, std::pair{kParamSpeed, 0.30},
                                   std::pair{kParamSize, 0.75}, std::pair{kParamDensity, 0.55},
                                   std::pair{kParamPitch, 0.52}, std::pair{kParamOverlap, 0.52},
                                   std::pair{kParamWindow, 0.3}, std::pair{kParamSpray, 0.25},
                                   std::pair{kParamPanRandom, 0.19}, std::pair{kParamDisplacement, 0.3},
                                   std::pair{kParamVolume, 0.75}})
        controller->setParamNormalized(id, value);

    const auto wave = MakeWave();
    const Case cases[] = {
        // Round 5: BLEND at 0, 50 and 100 % thickens the dark clouds; the layout open.
        {"blend_0", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kVioletGalaxy, false},
        {"blend_50", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kVioletGalaxy, false,
         kPresetAccent, kPresetWave, "down from.wav", true, MapView::Dust::kWisps, 0.5, 0.5},
        {"blend_100", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kVioletGalaxy, false,
         kPresetAccent, kPresetWave, "down from.wav", true, MapView::Dust::kWisps, 0.5, 1.0},
        {"layout_open", true, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kBlueGalaxy, true},
        {"layout_wave", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kOrangeGalaxy, false,
         kPresetAccent, kPresetWave, "down from.wav", false},
        // Round 3: the painted galaxies, one with HUE, one dense.
        {"galaxy_blue", true, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kBlueGalaxy, false},
        {"galaxy_orange_dense", false, kKeyboardDefaultTranspose, false, false, false, 0.0, true,
         GuiPreset::kOrangeGalaxy, false},
        {"galaxy_red", false, kKeyboardDefaultTranspose, false, false, false, 1.0, false, GuiPreset::kRedGalaxy, false},
        {"galaxy_violet_hue_blue", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false,
         GuiPreset::kVioletGalaxy, false, kAccentBlueHue},
        {"spiral_keys_lowest", true, kKeyboardLowestTranspose, true, false, false, -3.0},
        {"spiral_highest_stop", true, kKeyboardHighestTranspose, false, true, false, 0.0},
        {"spiral_dense_pitch_down", true, kKeyboardDefaultTranspose, false, false, false, -1.5, true},
        {"spiral_open", true, kKeyboardDefaultTranspose, true, false, false, 0.0, false, GuiPreset::kSpiral, true},
        {"spiral_closed", false, kKeyboardDefaultTranspose, false, false, true, 2.0, false, GuiPreset::kSpiral, false},
        {"barred_open", true, kKeyboardDefaultTranspose, true, false, false, 0.0, false, GuiPreset::kBarred, true},
        {"barred_closed", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kBarred, false},
        {"ring_open", true, kKeyboardDefaultTranspose, true, false, false, 0.0, false, GuiPreset::kRing, true},
        {"ring_closed", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kRing, false},
        {"ring_keys_only", true, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kRing, false},
        {"ring_details_only", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kRing, true},
        {"cloud_open", true, kKeyboardDefaultTranspose, true, false, false, 0.0, false, GuiPreset::kCloud, true},
        {"spiral_hue_red", true, kKeyboardDefaultTranspose, true, false, false, 0.0, false, GuiPreset::kSpiral, true, 0,
         160},
        {"cloud_hue_blue", true, kKeyboardDefaultTranspose, true, false, false, 0.0, false, GuiPreset::kCloud, true, 235,
         50},
        // Round 7: the waveform turned with the playback, a third and two thirds round.
        {"wave_turned_third", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kSpiral,
         false, kPresetAccent, kPresetWave, "down from.wav", false, MapView::Dust::kWisps, 0.5, 0.0, 0.33},
        {"wave_turned_two_thirds", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false,
         GuiPreset::kOrangeGalaxy, false, kPresetAccent, kPresetWave, "down from.wav", false, MapView::Dust::kWisps, 0.5,
         0.0, 0.67},
        // Beta 2: WAVE in the glass.
        {"spiral_wave_closed", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "down from.wav", false},
        {"spiral_wave_frozen", false, kKeyboardDefaultTranspose, false, false, true, 2.0, false, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "down from.wav", false},
        {"spiral_wave_dense", false, kKeyboardDefaultTranspose, false, false, false, -1.5, true, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "down from.wav", false},
        {"barred_wave_closed", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kBarred, false,
         kPresetAccent, kPresetWave, "down from.wav", false},
        {"ring_wave_closed", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kRing, false,
         kPresetAccent, kPresetWave, "down from.wav", false},
        {"cloud_wave_open", true, kKeyboardDefaultTranspose, true, false, false, 0.0, false, GuiPreset::kCloud, true,
         kPresetAccent, kPresetWave, "down from.wav", false},
        {"cloud_closed", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kCloud, false},
        // The dust experiment: the three masks at 0, 30, 50 and 70 % (GALAXY, closed; round 2: 50 % by default).
        {"dust_off", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "down from.wav", true, MapView::Dust::kWisps, 0.0},
        {"dust_wisps_30", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "down from.wav", true, MapView::Dust::kWisps, 0.30},
        {"dust_wisps_50", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "down from.wav", true, MapView::Dust::kWisps, 0.50},
        {"dust_wisps_70", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "down from.wav", true, MapView::Dust::kWisps, 0.70},
        {"dust_bands_50", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "down from.wav", true, MapView::Dust::kBands, 0.50},
        {"dust_veil_50", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "down from.wav", true, MapView::Dust::kVeil, 0.50},
        {"spiral_long_name", false, kKeyboardDefaultTranspose, false, false, false, 0.0, false, GuiPreset::kSpiral, false,
         kPresetAccent, kPresetWave, "Field Recording 2026-11-19, Rain on the Glasshouse Roof (Take 12).wav"},
    };
    for(const Case& c : cases) {
        controller->setParamNormalized(kParamStop, c.stop ? 1.0 : 0.0);
        controller->setParamNormalized(kParamFreeze, c.freeze ? 1.0 : 0.0);
        controller->setParamNormalized(kParamPitch, PitchNormalized(c.pitch));
        EditorSettings settings;
        settings.keyboard = c.keyboard;
        settings.transpose = c.transpose;
        settings.preset = c.preset;
        settings.details = c.details;
        settings.accents[static_cast<std::size_t>(c.preset)] = c.hue;
        settings.wave_percent = c.wave;
        settings.galaxy = c.galaxy;
        MapView::ChooseDust({c.dust, c.dust_strength});
        MapView::TurnNewViews(c.turn);
        controller->setParamNormalized(kParamBlend, c.blend);
        IPtr<Editor> editor(new Editor(controller, settings, EditorCallbacks{}), false);
        editor->ShowDisplay(MakeDisplay(c.freeze, c.dense), wave);
        FileStatus status;
        status.path = std::string("C:\\Samples\\") + c.file;
        status.sound = SoundState::kReady;
        status.info = "48 kHz  24 bit  stereo  24.00 s";
        status.serial = 1;
        editor->ShowFileStatus(status);
        if(editor->attached(parent, kPlatformTypeHWND) != kResultOk) {
            std::fprintf(stderr, "%s: the window did not open\n", c.name);
            ++failures;
            continue;
        }
        editor->SetLatch(c.latch);
        if(c.latch && editor->Keyboard())
            editor->Keyboard()->MouseDownOn(kKeyboardLowNote + c.transpose + 4); // a held E
        editor->ShowDisplay(MakeDisplay(c.freeze, c.dense), wave);
        for(const double scale : {1.0, 2.0, 0.5}) {
            char name[96];
            std::snprintf(name, sizeof name, "messier_%s_%gx.png", c.name, scale);
            if(!Render(*editor, scale, folder / name))
                ++failures;
        }
        if(editor->Keyboard())
            editor->Keyboard()->ReleaseHeld();
        static_cast<IPlugView*>(editor.get())->removed();
    }
    controller->terminate();
    controller = nullptr;
    if(parent)
        DestroyWindow(parent);
    VSTGUI::exit();
    return failures == 0 ? 0 : 1;
}
