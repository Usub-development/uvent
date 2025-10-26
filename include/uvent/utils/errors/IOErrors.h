//
// Created by kirill on 5/1/25.
//

#ifndef UVENT_IOERRORS_H
#define UVENT_IOERRORS_H

#include <string>
#include <stdexcept>
#include <array>
#include <cstddef>
#include <string_view>

namespace usub::utils::errors {
    enum class ConnectError {
        GetAddrInfoFailed,
        SocketCreationFailed,
        ConnectFailed,
        InvalidAddressFamily,
        InvalidSockType,
        FcntlFailed,
        EpollAddFailed,
        AlreadyConnected,
        InvalidHostname,
        Timeout,
        Unknown
    };

    enum class SendError {
        InvalidSocketFd,
        RecvFailed,
        RecvFromFailed,
        InvalidAddressVariant,
        Timeout,
        Closed,
        SendFailed
    };

    enum class SocketError
    {
        Timeout
    };

    constexpr const char *toString(SendError err) noexcept {
        switch (err) {
            case SendError::InvalidSocketFd:
                return "InvalidSocketFd";
            case SendError::RecvFailed:
                return "RecvFailed";
            case SendError::RecvFromFailed:
                return "RecvFromFailed";
            case SendError::InvalidAddressVariant:
                return "InvalidAddressVariant";
            case SendError::Timeout:
                return "Timeout";
            default:
                return "UnknownSendError";
        }
    }

    constexpr const char *toString(ConnectError err) noexcept {
        switch (err) {
            case ConnectError::GetAddrInfoFailed:
                return "GetAddrInfoFailed";
            case ConnectError::SocketCreationFailed:
                return "SocketCreationFailed";
            case ConnectError::ConnectFailed:
                return "ConnectFailed";
            case ConnectError::InvalidAddressFamily:
                return "InvalidAddressFamily";
            case ConnectError::InvalidSockType:
                return "InvalidSockType";
            case ConnectError::FcntlFailed:
                return "FcntlFailed";
            case ConnectError::EpollAddFailed:
                return "EpollAddFailed";
            case ConnectError::AlreadyConnected:
                return "AlreadyConnected";
            case ConnectError::InvalidHostname:
                return "InvalidHostname";
            case ConnectError::Timeout:
                return "Timeout";
            case ConnectError::Unknown:
                return "Unknown";
            default:
                return "UnknownConnectError";
        }
    }

    constexpr const char *toString(SocketError err) noexcept {
        switch (err) {
        case SocketError::Timeout:
            return "Timeout";
        default:
            return "UnknownSendError";
        }
    }
}

#endif //UVENT_IOERRORS_H
