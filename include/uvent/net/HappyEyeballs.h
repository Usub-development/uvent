//
// Happy Eyeballs (RFC 8305)
//

#ifndef UVENT_HAPPY_EYEBALLS_H
#define UVENT_HAPPY_EYEBALLS_H

#include "Socket.h"

#include <chrono>
#include <expected>
#include <string>
#include <vector>

namespace usub::uvent::net
{
    struct HappyEyeballsOptions
    {
        std::chrono::milliseconds attempt_delay{250};
        std::chrono::milliseconds attempt_timeout{10000};
        size_t max_attempts{4};
        std::chrono::milliseconds resolution_delay{50};
    };

    struct ResolvedAddr
    {
        std::string ip;
        utils::net::IPV ipv{utils::net::IPV4};
    };

    task::Awaitable<std::expected<TCPClientSocket, usub::utils::errors::ConnectError>>
    connect_happy_addrs(std::vector<ResolvedAddr> addrs, std::string port, HappyEyeballsOptions opts = {});

    task::Awaitable<std::expected<TCPClientSocket, usub::utils::errors::ConnectError>>
    connect_happy(std::string host, std::string port, HappyEyeballsOptions opts = {});
} // namespace usub::uvent::net

#endif // UVENT_HAPPY_EYEBALLS_H
