//
// Created by kirill on 11/11/24.
//

#include "include/uvent/poll/PollerBase.h"

namespace usub::uvent::core {
    PollerBase::PollerBase(uint64_t timeoutDuration_ms) : timeoutDuration_ms(timeoutDuration_ms) {}
}
