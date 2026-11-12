#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace taureon::toy_piano_classic {

// Generated once. Never change them: hosts find the plug-in in saved projects
// by these IDs.
static const Steinberg::FUID kProcessorUID(0x4EEE264D, 0xA73FB740, 0xAC97F0E1, 0x7F847535);
static const Steinberg::FUID kControllerUID(0xDC46709E, 0xC9B3DB44, 0xB159EFFF, 0x1ECE3376);

Steinberg::FUnknown* CreateProcessorInstance(void*);
Steinberg::FUnknown* CreateControllerInstance(void*);

} // namespace taureon::toy_piano_classic
