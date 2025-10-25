//
// Created by kirill on 8/30/24.
//

#include "uvent/utils/net/socket.h"

namespace usub::uvent::utils::socket {
    int createSocket(int port, const std::string &ip_addr, int backlog, net::IPV ipv, net::SocketAddressType socType) {
#if defined(OS_LINUX) || defined(OS_BSD)
        int soc_fd = ::socket((ipv == net::IPV::IPV4) ? AF_INET : AF_INET6,
                              (socType == net::SocketAddressType::TCP) ? SOCK_STREAM : SOCK_DGRAM, 0);
        if (soc_fd < 0) throw std::system_error(errno, std::generic_category(), "socket()");

#ifdef SO_REUSEPORT
        int reuse = 1;
        if (setsockopt(soc_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
            throw std::system_error(errno, std::generic_category(), "setsockopt(SO_REUSEADDR)");

        if (setsockopt(soc_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0)
            throw std::system_error(errno, std::generic_category(), "setsockopt(SO_REUSEPORT)");
#endif

        if (ipv == net::IPV::IPV4) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = (ip_addr == "127.0.0.1") ? htonl(INADDR_LOOPBACK) : inet_addr(ip_addr.c_str());

            if (bind(soc_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
                throw std::system_error(errno, std::generic_category(), "bind()");
        } else {
            sockaddr_in6 addr{};
            addr.sin6_family = AF_INET6;
            addr.sin6_port = htons(port);
            if (inet_pton(AF_INET6, ip_addr.c_str(), &addr.sin6_addr) <= 0)
                throw std::system_error(errno, std::generic_category(), "inet_pton(AF_INET6)");

            if (bind(soc_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
                throw std::system_error(errno, std::generic_category(), "bind()");
        }

        if (socType == net::SocketAddressType::TCP) {
            if (listen(soc_fd, backlog) < 0)
                throw std::system_error(errno, std::generic_category(), "listen()");
        }

        if (int flags; (flags = fcntl(soc_fd, F_GETFL, 0)) < 0 || fcntl(soc_fd, F_SETFL, flags | O_NONBLOCK) < 0)
            throw std::system_error(errno, std::generic_category(), "fcntl()");

        return soc_fd;
#else
#error "Windows isn't supported yet"
#endif
    }

    void makeSocketNonBlocking(int soc_fd) {
#if defined(OS_LINUX) || defined(OS_BSD)
        if (int flags; (flags = fcntl(soc_fd, F_GETFL, 0)) < 0 || fcntl(soc_fd, F_SETFL, flags | O_NONBLOCK) < 0)
            throw std::system_error(errno, std::generic_category(), "fcntl()");
    }

#elif
#error "Windows isn't supported yet"
#endif
}