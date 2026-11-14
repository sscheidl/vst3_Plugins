#include "controller.h"
#include "plugin_ids.h"
#include "processor.h"
#include "version.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace torus_classic;

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Vst::kDistributable,
           Vst::PlugType::kInstrumentSynth,
           FULL_VERSION_STR,
           kVstVersionString,
           CreateProcessorInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           stringPluginName " Controller",
           0,
           "",
           FULL_VERSION_STR,
           kVstVersionString,
           CreateControllerInstance)

END_FACTORY
