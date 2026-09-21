#include "timbre.h"

namespace timbre {

namespace {
size_t gIndex = 0;
}

const Voice &current()
{
    return kVoices[gIndex];
}

void next()
{
    gIndex = (gIndex + 1) % kVoiceCount;
}

}  // namespace timbre
