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

#if defined(OS_LINUX) || defined(OS_BSD) || defined(OS_APPLE)
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace usub::uvent::utils::socket {

    socket_fd_t createSocket(int port,
                             const std::string &ip_addr,
                             int backlog,
                             net::IPV ipv,
                             net::SocketAddressType socType);

    inline bool makeSocketNonBlocking(socket_fd_t fd) {
#if defined(OS_LINUX) || defined(OS_BSD) || defined(OS_APPLE)
        int fl = ::fcntl(fd, F_GETFL, 0);
        if (fl == -1 || ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) == -1)
            return false;

        int fdfl = ::fcntl(fd, F_GETFD, 0);
        if (fdfl == -1 || ::fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC) == -1)
            return false;

        return true;

#elif defined(OS_WINDOWS)
        u_long mode = 1;
        if (::ioctlsocket(fd, FIONBIO, &mode) != 0)
            return false;

        HANDLE h = reinterpret_cast<HANDLE>(fd);
        if (!::SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0))
            return false;

        return true;

#else
        return false;
#endif
    }

} // namespace usub::uvent::utils::socket

#endif // UVENT_SOCKETBASE_H