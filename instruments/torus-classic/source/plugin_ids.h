#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace torus_classic {

// Generated once for Torus Classic. Never change them: hosts find the plug-in
// in saved projects by these IDs.
static const Steinberg::FUID kProcessorUID(0x1385AD46, 0x5D5A4FAA, 0x9392E1F1, 0x04BCF372);
static const Steinberg::FUID kControllerUID(0x5203BCC0, 0x7FD24B79, 0xA399D335, 0xE3AB7DA2);

Steinberg::FUnknown* CreateProcessorInstance(void*);
Steinberg::FUnknown* CreateControllerInstance(void*);

} // namespace torus_classic
