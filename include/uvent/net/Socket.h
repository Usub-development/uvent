//
// Created by root on 9/11/25.
//

#ifndef NEWSOCKET_H
#define NEWSOCKET_H

#if defined(__linux__) && defined(UVENT_ENABLE_IO_URING)
    #include "SocketLinuxIOUring.h"
#elif defined(__linux__)
    #include "SocketLinux.h"
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    // BSD + macOS (darwin)
#include "SocketBSD.h"
#elif defined(_WIN32)
#include "SocketWindows.h"
#else
    #error "Unsupported platform"
#endif

#endif  // NEWSOCKET_H