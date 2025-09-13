//
// Created by kirill on 8/30/24.
//

#ifndef UVENT_SOCKETBASE_H
#define UVENT_SOCKETBASE_H

// USUB:
#include "net.h"
#include "include/uvent/system/Defines.h"

// STL:
#include <string>
#include <system_error>

namespace usub::uvent::utils::socket {
    int createSocket(int port, const std::string &ip_addr, int backlog, net::IPV ipv, net::SocketAddressType socType);

    void makeSocketNonBlocking(int soc_fd);
}

#endif //UVENT_SOCKETBASE_H
