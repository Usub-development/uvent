//
// Created by kirill on 11/11/24.
//

#ifndef UVENT_POLLERBASE_H
#define UVENT_POLLERBASE_H

#include <memory>
#include <semaphore>
#include "uvent/net/SocketMetadata.h"

namespace usub::uvent::core
{
#ifdef OS_LINUX
    using PollerImpl = core::EPoller;
#elif defined(OS_BSD) || defined(OS_APPLE)
    using PollerImpl = core::KQueuePoller;
#else
    using PollerImpl = core::IocpPoller;
#endif
    enum OperationType
    {
        READ = 1 << 0,
        WRITE = 1 << 1,
        ALL = 3
    };

    enum ActionPolicy
    {
        SINGLE_THREAD,
        MULTI_THREAD
    };
}

#endif //UVENT_POLLERBASE_H
