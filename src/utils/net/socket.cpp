//
// Created by kirill on 8/30/24.
//

#include "uvent/utils/net/socket.h"
#include <system_error>

#if defined(_WIN32)
#include <WS2tcpip.h>
#include <WinSock2.h>
// (optional) #pragma comment(lib, "Ws2_32.lib")

namespace {
inline void throw_wsa(const char *what) { // I dont know what's that sorry
  int err = WSAGetLastError();
  throw std::system_error(err, std::system_category(), what);
}
} // namespace
#endif

namespace usub::uvent::utils::socket {
int createSocket(int port, const std::string &ip_addr, int backlog,
                 net::IPV ipv, net::SocketAddressType socType) {
#if defined(OS_LINUX) || defined(OS_BSD) || defined(OS_APPLE)
  int soc_fd = ::socket(
      (ipv == net::IPV::IPV4) ? AF_INET : AF_INET6,
      (socType == net::SocketAddressType::TCP) ? SOCK_STREAM : SOCK_DGRAM, 0);
  if (soc_fd < 0)
    throw std::system_error(errno, std::generic_category(), "socket()");

#ifdef SO_REUSEPORT
  int reuse = 1;
  if (setsockopt(soc_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    throw std::system_error(errno, std::generic_category(),
                            "setsockopt(SO_REUSEADDR)");

  if (setsockopt(soc_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0)
    throw std::system_error(errno, std::generic_category(),
                            "setsockopt(SO_REUSEPORT)");
#endif

  if (ipv == net::IPV::IPV4) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = (ip_addr == "127.0.0.1")
                               ? htonl(INADDR_LOOPBACK)
                               : inet_addr(ip_addr.c_str());

    if (bind(soc_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
      throw std::system_error(errno, std::generic_category(), "bind()");
  } else {
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    if (inet_pton(AF_INET6, ip_addr.c_str(), &addr.sin6_addr) <= 0)
      throw std::system_error(errno, std::generic_category(),
                              "inet_pton(AF_INET6)");

    if (bind(soc_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
      throw std::system_error(errno, std::generic_category(), "bind()");
  }

  if (socType == net::SocketAddressType::TCP) {
    if (listen(soc_fd, backlog) < 0)
      throw std::system_error(errno, std::generic_category(), "listen()");
  }

  if (int flags; (flags = fcntl(soc_fd, F_GETFL, 0)) < 0 ||
                 fcntl(soc_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    throw std::system_error(errno, std::generic_category(), "fcntl()");

  return soc_fd;
#else
  // TODO: check it
  // #error "Windows isn't supported yet"

  // EXPECTS: WSAStartup() already called once in your program.
  const int family = (ipv == net::IPV::IPV4) ? AF_INET : AF_INET6;
  const int type =
      (socType == net::SocketAddressType::TCP) ? SOCK_STREAM : SOCK_DGRAM;

  SOCKET s = ::socket(family, type, 0);
  if (s == INVALID_SOCKET)
    throw_wsa("socket()");

  // Address reuse semantics differ on Windows:
  // - SO_REUSEADDR on TCP is lax; for exclusive use prefer SO_EXCLUSIVEADDRUSE.
  // - There is no portable SO_REUSEPORT like Linux.
#if defined(UVENT_ENABLE_REUSEADDR)
  {
    BOOL on = TRUE;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&on),
                   sizeof(on)) == SOCKET_ERROR) {
      throw_wsa("setsockopt(SO_REUSEADDR)");
    }
  }
#else
  if (socType == net::SocketAddressType::TCP) {
    BOOL on = TRUE;
    // best-effort; ignore failure on very old systems
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&on), sizeof(on));
  }
#endif

  if (family == AF_INET) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (ip_addr.empty() || ip_addr == "0.0.0.0") {
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (ip_addr == "127.0.0.1") {
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
      if (InetPtonA(AF_INET, ip_addr.c_str(), &addr.sin_addr) != 1) {
        throw_wsa("InetPtonA(AF_INET)");
      }
    }

    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
        SOCKET_ERROR)
      throw_wsa("bind()");
  } else { // AF_INET6
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(static_cast<uint16_t>(port));

    if (ip_addr.empty() || ip_addr == "::") {
      addr.sin6_addr = in6addr_any;
    } else if (ip_addr == "::1") {
      addr.sin6_addr = in6addr_loopback;
    } else {
      if (InetPtonA(AF_INET6, ip_addr.c_str(), &addr.sin6_addr) != 1) {
        throw_wsa("InetPtonA(AF_INET6)");
      }
    }

    // Optional: restrict to v6 only (no v4-mapped). Enable if that’s your
    // desired behavior. BOOL v6only = TRUE; setsockopt(s, IPPROTO_IPV6,
    // IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof(v6only));

    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
        SOCKET_ERROR)
      throw_wsa("bind()");
  }

  if (socType == net::SocketAddressType::TCP) {
    if (listen(s, backlog) == SOCKET_ERROR)
      throw_wsa("listen()");
  }

  // Non-blocking
  u_long nb = 1;
  if (ioctlsocket(s, FIONBIO, &nb) == SOCKET_ERROR)
    throw_wsa("ioctlsocket(FIONBIO)");

  return static_cast<int>(s); // beware truncation on Win64
#endif
}
} // namespace usub::uvent::utils::socket
