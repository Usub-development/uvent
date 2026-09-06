//
// Created by root on 9/11/25.
//

#ifndef NEWSOCKET_H
#define NEWSOCKET_H

#if defined(__linux__) && defined(UVENT_ENABLE_IO_URING)
#include "SocketLinuxIOUring.h"
#elif defined(__linux__)
#include "SocketLinux.h"
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) ||                 \
    defined(__APPLE__)
// BSD + macOS (darwin)
#include "SocketBSD.h"
#elif defined(_WIN32)
#include "SocketWindows.h"
#else
#error "Unsupported platform"
#endif

#include "uvent/base/Traits.h"
#include "uvent/utils/timer/Timer.h"

namespace usub::uvent
{
    template <net::Proto p, net::Role r>
    struct is_thread_affine<net::Socket<p, r>> : std::true_type
    {
    };

    template <>
    struct is_thread_affine<utils::Timer> : std::true_type
    {
    };
} // namespace usub::uvent

#endif // NEWSOCKET_H
