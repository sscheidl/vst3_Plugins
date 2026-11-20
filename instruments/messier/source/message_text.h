// The texts of the messages between the two halves of the plug-in (0.2.7).
#pragma once

#include "pluginterfaces/vst/ivstattributes.h"

#include <string>

namespace messier {

// A text travels as its UTF-8 bytes in a binary attribute, so that no host converts it.
// An empty text is not sent at all, and a missing text reads as empty (0.2.7): a host may
// keep no binary attribute of length 0. Studio One behaves this way; until 0.2.7 the
// controller then refused every file status with an empty detail, and the window kept the
// file name it had from the project.

// False if a text that is not empty could not be stored (MS-R58): the sender then sends
// nothing rather than a message that reads as if the text were empty.
inline bool SetText(Steinberg::Vst::IAttributeList* attributes, const char* id, const std::string& text) {
    if(!attributes)
        return false;
    return text.empty()
           || attributes->setBinary(id, text.data(), static_cast<Steinberg::uint32>(text.size())) == Steinberg::kResultOk;
}

// A text that must be there and not be empty, such as the path of a load.
inline bool GetText(Steinberg::Vst::IAttributeList* attributes, const char* id, std::string& text) {
    const void* data = nullptr;
    Steinberg::uint32 size = 0;
    if(!attributes || attributes->getBinary(id, data, size) != Steinberg::kResultOk || size == 0 || !data)
        return false;
    text.assign(static_cast<const char*>(data), size);
    return true;
}

// A text that may be empty (the file status's path, info, request and detail): missing
// reads as empty. False only for an attribute list that is missing, or a size without data.
inline bool GetOptionalText(Steinberg::Vst::IAttributeList* attributes, const char* id, std::string& text) {
    if(!attributes)
        return false;
    const void* data = nullptr;
    Steinberg::uint32 size = 0;
    if(attributes->getBinary(id, data, size) != Steinberg::kResultOk || size == 0) {
        text.clear();
        return true;
    }
    if(!data)
        return false;
    text.assign(static_cast<const char*>(data), size);
    return true;
}

} // namespace messier
