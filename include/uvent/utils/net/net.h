//
// Created by kirill on 8/30/24.
//

#ifndef UVENT_NET_H
#define UVENT_NET_H

namespace usub::uvent::utils::net {
    enum IPV {
        IPV4 = 0x0,
        IPV6 = 0x1,
    };

    enum SocketAddressType {
        TCP = 0x0,
        UDP = 0x1,
    };

    enum SocketState {
        ACCEPTOR,
        SERVER,
        CONNECTION,
        CLIENT,
    };
}

#endif //UVENT_NET_H
