//
// Created by kirill on 8/30/24.
//

#ifndef UVENT_SOCKETBASE_H
#define UVENT_SOCKETBASE_H

// USUB:
#include "net.h"
#include "uvent/system/Defines.h"

// STL:
#include <string>
#include <system_error>

namespace usub::uvent::utils::socket {
    int createSocket(int port, const std::string &ip_addr, int backlog, net::IPV ipv, net::SocketAddressType socType);

    inline bool makeSocketNonBlocking(int fd) {
#if defined(OS_LINUX) || defined(OS_BSD) || defined(OS_APPLE)
        int fl = ::fcntl(fd, F_GETFL, 0);
        if (fl == -1 || ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) == -1)
            return false;

        int fdfl = ::fcntl(fd, F_GETFD, 0);
        if (fdfl == -1 || ::fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC) == -1)
            return false;
        return true;
#else
#  error "Windows isn't supported yet"
#endif
    }
}

#endif //UVENT_SOCKETBASE_H
