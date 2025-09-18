//
// Created by kirill on 11/16/24.
//

#ifndef UVENT_KPOLLER_H
#define UVENT_KPOLLER_H

#if 0
#include <mutex>
#include "uvent/poll/PollerBase.h"

namespace usub::uvent::core {
    /**
    * \brief Used on BSD systems (e.g OpenBSD, FreeBSD, MacOS, NetBSD etc.). Wrapper over kqueue.
    */
    class KPoller : public PollerBase {
    public:
        explicit KPoller(uint64_t timeoutDuration_ms);
    };
}
#endif

#endif //UVENT_KPOLLER_H
