// The map of the sample (since 0.1.4, provisional until the Atlas/Orion window):
// across, the position in the file; in the upper part the panorama of the sounding
// grains (left at the top), the loop, the spray range around the playhead and the
// playhead itself; below, in the same scale, the waveform overview of the file the
// engine reads. A display only: it draws what the controller last received and
// takes no input.
#pragma once

#include "looper_engine.h"
#include "wave_overview.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include <memory>

namespace messier {

class MapView : public VSTGUI::CView {
  public:
    explicit MapView(const VSTGUI::CRect& size);

    // New data; redraws if it differs from what is shown. Never throws.
    void Show(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave) noexcept;
    void draw(VSTGUI::CDrawContext* context) override;

    // For tests: what the view would draw.
    const EngineDisplay& Shown() const { return display_; }
    bool ShowsWave() const { return wave_ != nullptr; }

  private:
    EngineDisplay display_{};
    std::shared_ptr<const WaveOverview> wave_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
};

} // namespace messier
