#include "controller.h"
#include "plugin_ids.h"
#include "processor.h"
#include "version.h"

#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace torus_classic;

namespace {

// The SDK's factory derives the Unicode class info from the 8-bit one byte by
// byte, so a UTF-8 accent would reach Unicode hosts as two wrong characters.
// A class registered as Unicode only would instead be missing for hosts that
// ask for the 8-bit info. So every class is registered with its ASCII name,
// and the Unicode info, which current hosts read first, then gets the name
// with the accent. Nothing else in either info differs.
class Factory : public CPluginFactory {
public:
    using CPluginFactory::CPluginFactory;

    void SetUnicodeName(int32 index, const char16* name) {
        if(index >= 0 && index < classCount)
            strncpy16(classes[index].info16.name, name, PClassInfo::kNameSize);
    }
};

}  // namespace

BEGIN_FACTORY_CLASS(Factory, stringCompanyName, stringCompanyWeb, stringCompanyEmail,
                    Vst::kDefaultFactoryFlags)

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

static_cast<Factory*>(gPluginFactory)->SetUnicodeName(0, stringPluginNameUtf16);
static_cast<Factory*>(gPluginFactory)->SetUnicodeName(1, stringPluginNameUtf16 u" Controller");

END_FACTORY
