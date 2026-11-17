#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace messier {

// Generated once for Messier on 2026-11-17. Never change them: hosts find the
// plug-in in saved projects by these IDs.
static const Steinberg::FUID kProcessorUID(0x43CBFFE0, 0xD8854AE8, 0x8611239B, 0x8F294A56);
static const Steinberg::FUID kControllerUID(0xCAB5C51E, 0xAFFF4C36, 0xA45CF126, 0x48CE1BAB);

Steinberg::FUnknown* CreateProcessorInstance(void*);
Steinberg::FUnknown* CreateControllerInstance(void*);

} // namespace messier
