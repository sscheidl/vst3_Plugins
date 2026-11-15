#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace taureon::toy_piano_generative {

// Generated once. Never change them: hosts find the plug-in in saved projects
// by these IDs.
static const Steinberg::FUID kProcessorUID(0xC1E20F88, 0x973E488F, 0xBC4B9796, 0x465BEC8D);
static const Steinberg::FUID kControllerUID(0xDA3B7198, 0x0A084AA1, 0x9CAA6F5F, 0x3FF861BF);

Steinberg::FUnknown* CreateProcessorInstance(void*);
Steinberg::FUnknown* CreateControllerInstance(void*);

} // namespace taureon::toy_piano_generative
