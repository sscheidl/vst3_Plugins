#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace messier {

// Generated once for Messier on 2026-11-17. Never change them: hosts find the
// plug-in in saved projects by these IDs.
#ifdef MESSIER_SIDE_BY_SIDE
// The side-by-side test build Messier2 (CMakeLists.txt, generated 2026-11-20): its own
// IDs, so that a host keeps it apart from Messier. Never in a release.
static const Steinberg::FUID kProcessorUID(0x501A4A23, 0x7861474B, 0x9BC6C4A2, 0x2F0101EF);
static const Steinberg::FUID kControllerUID(0x673B81B8, 0x768042D7, 0x851351E4, 0xFE59C349);
#else
static const Steinberg::FUID kProcessorUID(0x43CBFFE0, 0xD8854AE8, 0x8611239B, 0x8F294A56);
static const Steinberg::FUID kControllerUID(0xCAB5C51E, 0xAFFF4C36, 0xA45CF126, 0x48CE1BAB);
#endif

Steinberg::FUnknown* CreateProcessorInstance(void*);
Steinberg::FUnknown* CreateControllerInstance(void*);

} // namespace messier
