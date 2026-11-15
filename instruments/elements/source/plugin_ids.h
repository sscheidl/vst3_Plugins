#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace elements_synth {

// Generated once for this plug-in. Never change them: hosts find the plug-in
// in saved projects by these IDs.
static const Steinberg::FUID kProcessorUID(0x419AD745, 0x43C74420, 0xA606A40F, 0x1DE4FA7B);
static const Steinberg::FUID kControllerUID(0x43CEC3AF, 0x41924864, 0xB3076AE1, 0x6AF0085D);

Steinberg::FUnknown* CreateProcessorInstance(void*);
Steinberg::FUnknown* CreateControllerInstance(void*);

} // namespace elements_synth
