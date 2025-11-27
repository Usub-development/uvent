//
// Created by kirill on 11/27/25.
//

#ifndef SOCKETWINDOWS_H
#define SOCKETWINDOWS_H

#include <coroutine>
#include <expected>
#include <memory>

#include "AwaiterOperations.h"
#include "SocketMetadata.h"
#include "uvent/base/Predefines.h"
#include "uvent/system/Defines.h"
#include "uvent/system/SystemContext.h"
#include "uvent/utils/buffer/DynamicBuffer.h"
#include "uvent/utils/errors/IOErrors.h"
#include "uvent/utils/net/net.h"
#include "uvent/utils/net/socket.h"

namespace usub::uvent::net
{
    enum class IocpOp : uint8_t { READ, WRITE, ACCEPT, CONNECT };

    struct IocpOverlapped {
        OVERLAPPED ov{};
        SocketHeader* header{};
        IocpOp op{};
        DWORD bytes_transferred{};
    };

    namespace detail
    {
        extern void processSocketTimeout(std::any arg);
    }

    template <Proto p, Role r>
    class Socket : usub::utils::sync::refc::RefCounted<Socket<p, r>>
    {
    public:
        friend class usub::utils::sync::refc::RefCounted<Socket<p, r>>;
        friend class core::PollerBase;

        friend void detail::processSocketTimeout(std::any arg);

        Socket() noexcept;

        explicit Socket(socket_fd_t fd) noexcept;

        explicit Socket(std::string& ip_addr, int port = 8080, int backlog = 50,
                        utils::net::IPV ipv = utils::net::IPV4,
                        utils::net::SocketAddressType socketAddressType = utils::net::TCP) noexcept
            requires(p == Proto::TCP && r == Role::PASSIVE);

        explicit Socket(std::string&& ip_addr, int port = 8080, int backlog = 50,
                        utils::net::IPV ipv = utils::net::IPV4,
                        utils::net::SocketAddressType socketAddressType = utils::net::TCP) noexcept
            requires(p == Proto::TCP && r == Role::PASSIVE);

        explicit Socket(SocketHeader* header) noexcept;

        Socket(const Socket& o) noexcept;
        Socket(Socket&& o) noexcept;

        Socket& operator=(const Socket& o) noexcept;
        Socket& operator=(Socket&& o) noexcept;

        ~Socket();

        static Socket from_existing(SocketHeader* header);
        SocketHeader* get_raw_header();

        [[nodiscard]] task::Awaitable<
            std::optional<TCPClientSocket>,
            uvent::detail::AwaitableIOFrame<std::optional<TCPClientSocket>>>
        async_accept()
            requires(p == Proto::TCP && r == Role::PASSIVE);

        [[nodiscard]] task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>> async_read(
            utils::DynamicBuffer& buffer, size_t max_read_size)
            requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP));

        [[nodiscard]] task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>> async_read(
            uint8_t* buffer, size_t max_read_size)
            requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP));

        [[nodiscard]] task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>>
        async_write(uint8_t* buf, size_t sz)
            requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP));

        [[nodiscard]] ssize_t read(utils::DynamicBuffer& buffer, size_t max_read_size)
            requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP));

        [[nodiscard]] ssize_t write(uint8_t* buf, size_t sz)
            requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP));

        [[nodiscard]] task::Awaitable<
            std::optional<usub::utils::errors::ConnectError>,
            uvent::detail::AwaitableIOFrame<std::optional<usub::utils::errors::ConnectError>>>
        async_connect(std::string& host, std::string& port)
            requires(p == Proto::TCP && r == Role::ACTIVE);

        [[nodiscard]] task::Awaitable<
            std::optional<usub::utils::errors::ConnectError>,
            uvent::detail::AwaitableIOFrame<std::optional<usub::utils::errors::ConnectError>>>
        async_connect(std::string&& host, std::string&& port)
            requires(p == Proto::TCP && r == Role::ACTIVE);

        task::Awaitable<
            std::expected<size_t, usub::utils::errors::SendError>,
            uvent::detail::AwaitableIOFrame<std::expected<size_t, usub::utils::errors::SendError>>>
        async_send(uint8_t* buf, size_t sz)
            requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP));

        [[nodiscard]] std::expected<std::string, usub::utils::errors::SendError> send(
            uint8_t* buf, size_t sz, size_t chunkSize = 16384, size_t maxSize = 65536)
            requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP));

        [[nodiscard]] task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>>
        async_sendfile(int in_fd, off_t* offset, size_t count)
            requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP));

        [[nodiscard]] ssize_t sendfile(int in_fd, off_t* offset, size_t count)
            requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP));

        void update_timeout(timer_duration_t new_duration) const;
        void shutdown();

        void set_timeout_ms(timeout_t timeout = settings::timeout_duration_ms) const
            requires(p == Proto::TCP && r == Role::ACTIVE);

        std::expected<std::string, usub::utils::errors::SendError> receive(size_t chunk_size,
                                                                           size_t maxSize);

        [[nodiscard]] client_addr_t get_client_addr() const
            requires(p == Proto::TCP && r == Role::ACTIVE);

        [[nodiscard]] client_addr_t get_client_addr()
            requires(p == Proto::TCP && r == Role::ACTIVE);

        [[nodiscard]] utils::net::IPV get_client_ipv() const
            requires(p == Proto::TCP && r == Role::ACTIVE);

    protected:
        void destroy() noexcept override;
        void remove();

    private:
        size_t send_aux(uint8_t* buf, size_t size);

    public:
        client_addr_t address;
        utils::net::IPV ipv{utils::net::IPV4};

    private:
        SocketHeader* header_{nullptr};
    };

    template <Proto p, Role r>
    Socket<p, r>::Socket() noexcept
    {
        wsa_init_once();
        this->header_ = new SocketHeader{
            .fd = INVALID_FD,
            .socket_info = (static_cast<uint8_t>(Proto::TCP) | static_cast<uint8_t>(Role::ACTIVE) |
                            static_cast<uint8_t>(AdditionalState::CONNECTION_PENDING)),
            .state = (1 & usub::utils::sync::refc::COUNT_MASK)
        };
    }

    template <Proto p, Role r>
    Socket<p, r>::Socket(socket_fd_t fd) noexcept
    {
        wsa_init_once();
        this->header_ = new SocketHeader{
            .fd = fd,
            .socket_info = (static_cast<uint8_t>(Proto::TCP) | static_cast<uint8_t>(Role::ACTIVE) |
                            static_cast<uint8_t>(AdditionalState::CONNECTION_PENDING)),
            .state = (1 & usub::utils::sync::refc::COUNT_MASK)
        };
        system::this_thread::detail::pl->addEvent(this->header_, core::OperationType::ALL);
    }

    template <Proto p, Role r>
    Socket<p, r>::Socket(std::string& ip_addr, int port, int backlog, utils::net::IPV ipv_,
                         utils::net::SocketAddressType socketAddressType) noexcept
        requires(p == Proto::TCP && r == Role::PASSIVE)
    {
        wsa_init_once();
        this->header_ = new SocketHeader{
            .fd = utils::socket::createSocket(port, ip_addr, backlog, ipv_, socketAddressType),
            .socket_info = (uint8_t(p) | uint8_t(r)),
            .state = (1ull & usub::utils::sync::refc::COUNT_MASK)
        };

        u_long mode = 1;
        ::ioctlsocket(this->header_->fd, FIONBIO, &mode);

        system::this_thread::detail::pl->addEvent(this->header_, core::OperationType::READ);
    }

    template <Proto p, Role r>
    Socket<p, r>::Socket(std::string&& ip_addr, int port, int backlog, utils::net::IPV ipv_,
                         utils::net::SocketAddressType socketAddressType) noexcept
        requires(p == Proto::TCP && r == Role::PASSIVE)
        : Socket(ip_addr, port, backlog, ipv_, socketAddressType)
    {
    }

    template <Proto p, Role r>
    Socket<p, r>::Socket(SocketHeader* header) noexcept : header_(header)
    {
    }

    template <Proto p, Role r>
    Socket<p, r>::~Socket()
    {
        if (this->header_)
        {
            this->release();
#if UVENT_DEBUG
            spdlog::warn("Socket counter: {}, fd: {}",
                         (this->header_->state & usub::utils::sync::refc::COUNT_MASK),
                         (std::uint64_t)this->header_->fd);
#endif
        }
    }

    template <Proto p, Role r>
    Socket<p, r>::Socket(const Socket& o) noexcept : header_(o.header_)
    {
        if (this->header_) this->add_ref();
    }

    template <Proto p, Role r>
    Socket<p, r>::Socket(Socket&& o) noexcept : header_(o.header_)
    {
        o.header_ = nullptr;
    }

    template <Proto p, Role r>
    Socket<p, r>& Socket<p, r>::operator=(const Socket& o) noexcept
    {
        if (this == &o) return *this;
        Socket tmp(o);
        std::swap(this->header_, tmp.header_);
        return *this;
    }

    template <Proto p, Role r>
    Socket<p, r>& Socket<p, r>::operator=(Socket&& o) noexcept
    {
        if (this == &o) return *this;
        Socket tmp(std::move(o));
        std::swap(this->header_, tmp.header_);
        return *this;
    }

    template <Proto p, Role r>
    Socket<p, r> Socket<p, r>::from_existing(SocketHeader* header)
    {
        return Socket(header);
    }

    template <Proto p, Role r>
    SocketHeader* Socket<p, r>::get_raw_header()
    {
        return this->header_;
    }

    template <Proto p, Role r>
    task::Awaitable<std::optional<TCPClientSocket>,
                    uvent::detail::AwaitableIOFrame<std::optional<TCPClientSocket>>>
    Socket<p, r>::async_accept()
        requires(p == Proto::TCP && r == Role::PASSIVE)
    {
        for (;;)
        {
            sockaddr_storage ss{};
            int sl = sizeof(ss);

            SOCKET cfd = ::accept(this->header_->fd,
                                  reinterpret_cast<sockaddr*>(&ss),
                                  &sl);

            if (cfd != INVALID_SOCKET)
            {
                u_long mode = 1;
                ::ioctlsocket(cfd, FIONBIO, &mode);

                auto* h =
                    new SocketHeader{
                        .fd = cfd,
                        .socket_info = uint8_t(Proto::TCP) | uint8_t(Role::ACTIVE),
                        .state = (1ull & usub::utils::sync::refc::COUNT_MASK)
                    };
                system::this_thread::detail::pl->addEvent(h, core::OperationType::READ);

                TCPClientSocket sc(h);
                if (ss.ss_family == AF_INET)
                    sc.address = *reinterpret_cast<sockaddr_in*>(&ss);
                else if (ss.ss_family == AF_INET6)
                {
                    sc.address = *reinterpret_cast<sockaddr_in6*>(&ss);
                    sc.ipv = utils::net::IPV6;
                }
                co_return sc;
            }

            int err = WSAGetLastError();
            switch (err)
            {
                case WSAEINTR:
                    continue;
                case WSAECONNABORTED:
                    continue;
                case WSAEWOULDBLOCK:
                    co_await detail::AwaiterAccept{this->header_};
                    continue;
                case WSAENOBUFS:
                case WSAEMFILE:
                    co_await detail::AwaiterAccept{this->header_};
                    continue;
                default:
                    co_return std::nullopt;
            }
        }
    }

    template <Proto p, Role r>
    task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>>
    Socket<p, r>::async_read(utils::DynamicBuffer& buffer, size_t max_read_size)
        requires ((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP))
    {
#if UVENT_DEBUG
        spdlog::info("Entered into read coroutine (win): fd={}", (std::uint64_t)this->header_->fd);
#endif
        if constexpr (p == Proto::UDP)
        {
            int retries = 0;
            ssize_t total_read = 0;

            while (true)
            {
                uint8_t temp[16384];
                size_t remaining = max_read_size - buffer.size();
                if (remaining == 0) break;

                size_t to_read = std::min(sizeof(temp), remaining);

                int res = ::recv(this->header_->fd,
                                 reinterpret_cast<char*>(temp),
                                 static_cast<int>(to_read),
                                 0);

                if (res > 0)
                {
                    buffer.append(temp, res);
                    total_read += res;
                    retries = 0;
                }
                else if (res == 0)
                {
                    this->remove();
                    co_return total_read > 0 ? total_read : 0;
                }
                else
                {
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK)
                    {
                        break;
                    }
                    else if (err == WSAEINTR)
                    {
                        if (++retries >= settings::max_read_retries)
                        {
                            this->remove();
                            co_return -1;
                        }
                        continue;
                    }
                    else
                    {
                        this->remove();
                        co_return -1;
                    }
                }

                if (buffer.size() >= max_read_size)
                    break;
            }
#ifndef UVENT_ENABLE_REUSEADDR
            if (total_read > 0) this->header_->timeout_epoch_bump();
#endif
            co_return total_read;
        }
        else
        {
            size_t remaining = max_read_size - buffer.size();
            if (remaining == 0)
                co_return 0;

            auto ov = std::make_unique<IocpOverlapped>();
            ov->header = this->header_;
            ov->op = IocpOp::READ;

            auto tmp = std::make_unique<uint8_t[]>(remaining);
            WSABUF wbuf;
            wbuf.buf = reinterpret_cast<char*>(tmp.get());
            wbuf.len = static_cast<ULONG>(remaining);

            DWORD flags = 0;
            DWORD bytes = 0;

            int rc = ::WSARecv(this->header_->fd, &wbuf, 1, &bytes, &flags, &ov->ov, nullptr);
            if (rc == SOCKET_ERROR)
            {
                int err = WSAGetLastError();
                if (err != WSA_IO_PENDING)
                {
                    this->remove();
                    co_return -1;
                }
            }

            if (rc == 0)
            {
                if (bytes == 0)
                {
                    this->remove();
                    co_return 0;
                }
                buffer.append(tmp.get(), bytes);
#ifndef UVENT_ENABLE_REUSEADDR
                if (bytes > 0) this->header_->timeout_epoch_bump();
#endif
                co_return static_cast<ssize_t>(bytes);
            }

            co_await detail::AwaiterRead{this->header_};

            flags = 0;
            if (!::WSAGetOverlappedResult(this->header_->fd, &ov->ov, &bytes, FALSE, &flags))
            {
                this->remove();
                co_return -1;
            }

            if (bytes == 0)
            {
                this->remove();
                co_return 0;
            }

            buffer.append(tmp.get(), bytes);
#ifndef UVENT_ENABLE_REUSEADDR
            if (bytes > 0) this->header_->timeout_epoch_bump();
#endif
            co_return static_cast<ssize_t>(bytes);
        }
    }

    template <Proto p, Role r>
    task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>>
    Socket<p, r>::async_read(uint8_t* dst, size_t max_read_size)
        requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP))
    {
#if UVENT_DEBUG
        spdlog::info("Entered into read coroutine (win raw): fd={}", (std::uint64_t)this->header_->fd);
#endif
        if (!dst || max_read_size == 0)
            co_return 0;

        if constexpr (p == Proto::UDP)
        {
            ssize_t total_read = 0;
            int retries = 0;

            for (;;)
            {
                int res = ::recvfrom(this->header_->fd,
                                     reinterpret_cast<char*>(dst),
                                     static_cast<int>(max_read_size),
                                     0,
                                     nullptr,
                                     nullptr);

                if (res > 0)
                {
#ifndef UVENT_ENABLE_REUSEADDR
                    this->header_->timeout_epoch_bump();
#endif
                    co_return res;
                }

                if (res == 0)
                {
                    co_return 0;
                }

                int err = WSAGetLastError();
                if (err == WSAEINTR)
                {
                    if (++retries >= settings::max_read_retries)
                    {
                        this->remove();
                        co_return -1;
                    }
                    continue;
                }

                if (err == WSAEWOULDBLOCK)
                    co_return 0;

                this->remove();
                co_return -1;
            }
        }
        else
        {
            auto ov = std::make_unique<IocpOverlapped>();
            ov->header = this->header_;
            ov->op = IocpOp::READ;

            WSABUF wbuf;
            wbuf.buf = reinterpret_cast<char*>(dst);
            wbuf.len = static_cast<ULONG>(max_read_size);

            DWORD flags = 0;
            DWORD bytes = 0;

            int rc = ::WSARecv(this->header_->fd, &wbuf, 1, &bytes, &flags, &ov->ov, nullptr);
            if (rc == SOCKET_ERROR)
            {
                int err = WSAGetLastError();
                if (err != WSA_IO_PENDING)
                {
                    this->remove();
                    co_return -1;
                }
            }

            if (rc == 0)
            {
                if (bytes == 0)
                {
                    this->remove();
                    co_return 0;
                }
#ifndef UVENT_ENABLE_REUSEADDR
                if (bytes > 0) this->header_->timeout_epoch_bump();
#endif
                co_return static_cast<ssize_t>(bytes);
            }

            co_await detail::AwaiterRead{this->header_};

            flags = 0;
            if (!::WSAGetOverlappedResult(this->header_->fd, &ov->ov, &bytes, FALSE, &flags))
            {
                this->remove();
                co_return -1;
            }

            if (bytes == 0)
            {
                this->remove();
                co_return 0;
            }
#ifndef UVENT_ENABLE_REUSEADDR
            if (bytes > 0) this->header_->timeout_epoch_bump();
#endif
            co_return static_cast<ssize_t>(bytes);
        }
    }

    template <Proto p, Role r>
    task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>>
    Socket<p, r>::async_write(uint8_t* buf, size_t sz)
        requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP))
    {
#if UVENT_DEBUG
        spdlog::info("Entered into write coroutine (win): fd={}, sz={}",
                     (std::uint64_t)this->header_->fd, sz);
#endif
        if (!buf || sz == 0)
        {
            co_return 0;
        }

        if constexpr (p == Proto::UDP)
        {
            int retries = 0;
            for (;;)
            {
                int res = ::send(this->header_->fd,
                                 reinterpret_cast<const char*>(buf),
                                 static_cast<int>(sz),
                                 0);
                if (res >= 0)
                {
#ifndef UVENT_ENABLE_REUSEADDR
                    if (res > 0) this->header_->timeout_epoch_bump();
#endif
                    co_return res;
                }
                int err = WSAGetLastError();
                if (err == WSAEINTR)
                {
                    if (++retries >= settings::max_write_retries) co_return -1;
                    continue;
                }
                if (err == WSAEWOULDBLOCK)
                {
                    co_return 0;
                }
                co_return -1;
            }
        }
        else
        {
            ssize_t total_written = 0;
            auto ov = std::make_unique<IocpOverlapped>();
            ov->header = this->header_;
            ov->op = IocpOp::WRITE;

            while (total_written < static_cast<ssize_t>(sz))
            {
                std::memset(&ov->ov, 0, sizeof(ov->ov));

                WSABUF wbuf;
                wbuf.buf = reinterpret_cast<char*>(buf + total_written);
                wbuf.len = static_cast<ULONG>(sz - static_cast<size_t>(total_written));

                DWORD bytes = 0;
                int rc = ::WSASend(this->header_->fd, &wbuf, 1, &bytes, 0, &ov->ov, nullptr);
                if (rc == SOCKET_ERROR)
                {
                    int err = WSAGetLastError();
                    if (err != WSA_IO_PENDING)
                    {
                        co_return -1;
                    }
                }

                if (rc == 0)
                {
                    if (bytes == 0)
                    {
                        co_return -1;
                    }
                    total_written += bytes;
#ifndef UVENT_ENABLE_REUSEADDR
                    if (bytes > 0) this->header_->timeout_epoch_bump();
#endif
                    continue;
                }

                co_await detail::AwaiterWrite{this->header_};

                DWORD flags = 0;
                if (!::WSAGetOverlappedResult(this->header_->fd, &ov->ov, &bytes, FALSE, &flags))
                {
                    co_return -1;
                }

                if (bytes == 0)
                {
                    co_return -1;
                }

                total_written += bytes;
#ifndef UVENT_ENABLE_REUSEADDR
                if (bytes > 0) this->header_->timeout_epoch_bump();
#endif
            }

            co_return total_written;
        }
    }

    template <Proto p, Role r>
    ssize_t Socket<p, r>::read(utils::DynamicBuffer& buffer, size_t max_read_size)
        requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP))
    {
        ssize_t total_read = 0;
        int retries = 0;

        while (true)
        {
            uint8_t temp[16384];
            size_t remaining = max_read_size - buffer.size();
            if (remaining == 0) break;

            size_t to_read = std::min(sizeof(temp), remaining);

            int res = ::recv(this->header_->fd,
                             reinterpret_cast<char*>(temp),
                             static_cast<int>(to_read),
                             0);

            if (res > 0)
            {
                buffer.append(temp, res);
                total_read += res;
                retries = 0;
            }
            else if (res == 0)
            {
                return total_read > 0 ? total_read : 0;
            }
            else
            {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK)
                {
                    break;
                }
                else if (err == WSAEINTR)
                {
                    if (++retries >= settings::max_read_retries)
                    {
                        return -1;
                    }
                    continue;
                }
                else
                {
                    return -1;
                }
            }
        }
        return total_read;
    }

    template <Proto p, Role r>
    ssize_t Socket<p, r>::write(uint8_t* buf, size_t sz)
        requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP))
    {
        auto buf_internal =
            std::unique_ptr<uint8_t[]>(new uint8_t[sz], std::default_delete<uint8_t[]>());
        std::copy_n(buf, sz, buf_internal.get());

        ssize_t total_written = 0;
        int retries = 0;

        while (total_written < static_cast<ssize_t>(sz))
        {
            int res = ::send(this->header_->fd,
                             reinterpret_cast<const char*>(buf_internal.get() + total_written),
                             static_cast<int>(sz - total_written),
                             0);
            if (res > 0)
            {
                total_written += res;
                retries = 0;
                continue;
            }
            else if (res == SOCKET_ERROR)
            {
                int err = WSAGetLastError();
                if (err == WSAEINTR)
                {
                    if (++retries >= settings::max_write_retries)
                    {
                        return -1;
                    }
                    continue;
                }
                else if (err == WSAEWOULDBLOCK)
                {
                    break;
                }
                else
                {
                    return -1;
                }
            }
        }
        return total_written;
    }

    template <Proto p, Role r>
    task::Awaitable<
        std::optional<usub::utils::errors::ConnectError>,
        uvent::detail::AwaitableIOFrame<std::optional<usub::utils::errors::ConnectError>>>
    Socket<p, r>::async_connect(std::string& host, std::string& port)
        requires(p == Proto::TCP && r == Role::ACTIVE)
    {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = (this->ipv == utils::net::IPV::IPV4) ? AF_INET : AF_INET6;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
        {
            this->header_->fd = INVALID_FD;
            co_return usub::utils::errors::ConnectError::GetAddrInfoFailed;
        }

        SOCKET s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s == INVALID_SOCKET)
        {
            ::freeaddrinfo(res);
            co_return usub::utils::errors::ConnectError::SocketCreationFailed;
        }

        this->header_->fd = s;

        u_long mode = 1;
        ::ioctlsocket(s, FIONBIO, &mode);

        if (res->ai_family == AF_INET)
            this->address = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
        else
            this->address = *reinterpret_cast<sockaddr_in6*>(res->ai_addr);

        int ret = ::connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen));
        if (ret < 0)
        {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS)
            {
                ::closesocket(s);
                this->header_->fd = INVALID_FD;
                ::freeaddrinfo(res);
                co_return usub::utils::errors::ConnectError::ConnectFailed;
            }
        }

        system::this_thread::detail::pl->addEvent(this->header_, core::OperationType::ALL);

        co_await detail::AwaiterWrite{this->header_};

        ::freeaddrinfo(res);

        if (this->header_->socket_info & static_cast<uint8_t>(AdditionalState::CONNECTION_FAILED))
            co_return usub::utils::errors::ConnectError::Unknown;

        this->header_->timeout_epoch_bump();
        co_return std::nullopt;
    }

    template <Proto p, Role r>
    task::Awaitable<
        std::optional<usub::utils::errors::ConnectError>,
        uvent::detail::AwaitableIOFrame<std::optional<usub::utils::errors::ConnectError>>>
    Socket<p, r>::async_connect(std::string&& host, std::string&& port)
        requires(p == Proto::TCP && r == Role::ACTIVE)
    {
        co_return co_await async_connect(host, port);
    }

    template <Proto p, Role r>
    task::Awaitable<
        std::expected<size_t, usub::utils::errors::SendError>,
        uvent::detail::AwaitableIOFrame<std::expected<size_t, usub::utils::errors::SendError>>>
    Socket<p, r>::async_send(uint8_t* buf, size_t sz)
        requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP))
    {
        auto buf_internal = std::unique_ptr<uint8_t[]>(new uint8_t[sz]);
        std::memcpy(buf_internal.get(), buf, sz);

        ssize_t total_written = 0;
        int retries = 0;

        while (total_written < static_cast<ssize_t>(sz))
        {
            for (;;)
            {
                if (this->is_disconnected_now())
                    co_return std::unexpected(usub::utils::errors::SendError::Closed);

                int res = 0;

                if constexpr (p == Proto::TCP)
                {
                    res = ::send(this->header_->fd,
                                 reinterpret_cast<const char*>(buf_internal.get() + total_written),
                                 static_cast<int>(sz - static_cast<size_t>(total_written)),
                                 0);
                }
                else
                {
                    try
                    {
                        if (std::holds_alternative<sockaddr_in>(this->address))
                        {
                            auto& addr = std::get<sockaddr_in>(this->address);
                            int addr_len = sizeof(sockaddr_in);
                            res = ::sendto(this->header_->fd,
                                           reinterpret_cast<const char*>(buf_internal.get() + total_written),
                                           static_cast<int>(sz - static_cast<size_t>(total_written)),
                                           0,
                                           reinterpret_cast<sockaddr*>(&addr),
                                           addr_len);
                        }
                        else if (std::holds_alternative<sockaddr_in6>(this->address))
                        {
                            auto& addr = std::get<sockaddr_in6>(this->address);
                            int addr_len = sizeof(sockaddr_in6);
                            res = ::sendto(this->header_->fd,
                                           reinterpret_cast<const char*>(buf_internal.get() + total_written),
                                           static_cast<int>(sz - static_cast<size_t>(total_written)),
                                           0,
                                           reinterpret_cast<sockaddr*>(&addr),
                                           addr_len);
                        }
                        else
                        {
                            co_return std::unexpected(
                                usub::utils::errors::SendError::InvalidAddressVariant);
                        }
                    }
                    catch (const std::bad_variant_access&)
                    {
                        co_return std::unexpected(
                            usub::utils::errors::SendError::InvalidAddressVariant);
                    }
                }

                if (res > 0)
                {
                    total_written += res;
                    retries = 0;

                    if (total_written >= static_cast<ssize_t>(sz))
                        goto send_done;

                    continue;
                }

                if (res == 0)
                {
                    this->remove();
                    co_return std::unexpected(usub::utils::errors::SendError::SendFailed);
                }

                int err = WSAGetLastError();
                if (err == WSAEINTR)
                {
                    if (++retries >= settings::max_write_retries)
                    {
                        this->remove();
                        co_return std::unexpected(usub::utils::errors::SendError::SendFailed);
                    }
                    continue;
                }

                if (err == WSAEWOULDBLOCK)
                {
                    break;
                }

                this->remove();
                co_return std::unexpected(usub::utils::errors::SendError::SendFailed);
            }

            co_await detail::AwaiterWrite{this->header_};
        }

    send_done:
#ifndef UVENT_ENABLE_REUSEADDR
        if (total_written > 0)
            this->header_->timeout_epoch_bump();
#endif
        co_return static_cast<size_t>(total_written);
    }

    template <Proto p, Role r>
    std::expected<std::string, usub::utils::errors::SendError> Socket<p, r>::send(
        uint8_t* buf, size_t sz, size_t chunkSize, size_t maxSize)
        requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP))
    {
        auto buf_internal =
            std::unique_ptr<uint8_t[]>(new uint8_t[sz], std::default_delete<uint8_t[]>());
        std::copy_n(buf, sz, buf_internal.get());
        auto sendRes = this->send_aux(buf_internal.get(), sz);
        if (sendRes != static_cast<size_t>(-1))
            return std::move(this->receive(chunkSize, maxSize));
        return std::unexpected(usub::utils::errors::SendError::InvalidSocketFd);
    }

    template <Proto p, Role r>
    task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>>
    Socket<p, r>::async_sendfile(int in_fd, off_t* offset, size_t count)
        requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP))
    {
        auto ov = std::make_unique<IocpOverlapped>();
        ov->header = this->header_;
        ov->op = IocpOp::WRITE;

        co_await detail::AwaiterWrite{this->header_};
        if (this->is_disconnected_now()) co_return -3;

        HANDLE hFile = (HANDLE)_get_osfhandle(in_fd);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            co_return -1;
        }

        BOOL ok = ::TransmitFile(this->header_->fd,
                                 hFile,
                                 static_cast<DWORD>(count),
                                 0,
                                 &ov->ov,
                                 nullptr,
                                 0);
        if (!ok)
        {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING)
            {
#if UVENT_DEBUG
                spdlog::debug("TransmitFile(win) error: {}", err);
#endif
                co_return -1;
            }
        }

        DWORD bytes = 0;
        DWORD flags = 0;
        if (!::WSAGetOverlappedResult(this->header_->fd, &ov->ov, &bytes, TRUE, &flags))
        {
#if UVENT_DEBUG
            spdlog::debug("TransmitFile(win) overlapped result error: {}", WSAGetLastError());
#endif
            co_return -1;
        }

#ifndef UVENT_ENABLE_REUSEADDR
        if (bytes > 0) this->header_->timeout_epoch_bump();
#endif

        if (offset) *offset += static_cast<off_t>(bytes);
        co_return static_cast<ssize_t>(bytes);
    }

    template <Proto p, Role r>
    ssize_t Socket<p, r>::sendfile(int in_fd, off_t* offset, size_t count)
        requires((p == Proto::TCP && r == Role::ACTIVE) || (p == Proto::UDP))
    {
        HANDLE hFile = (HANDLE)_get_osfhandle(in_fd);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            return -1;
        }

        BOOL ok = ::TransmitFile(this->header_->fd,
                                 hFile,
                                 static_cast<DWORD>(count),
                                 0,
                                 nullptr,
                                 nullptr,
                                 0);
        if (!ok)
        {
#if UVENT_DEBUG
            spdlog::debug("TransmitFile(win) error: {}", WSAGetLastError());
#endif
            return -1;
        }

        if (offset) *offset += static_cast<off_t>(count);
        return static_cast<ssize_t>(count);
    }

    template <Proto p, Role r>
    void Socket<p, r>::update_timeout(timer_duration_t new_duration) const
    {
        system::this_thread::detail::wh->updateTimer(this->header_->timer_id, new_duration);
    }

    template <Proto p, Role r>
    void Socket<p, r>::shutdown()
    {
        ::shutdown(this->header_->fd, SD_BOTH);
    }

    template <Proto p, Role r>
    void Socket<p, r>::set_timeout_ms(timeout_t timeout) const
        requires(p == Proto::TCP && r == Role::ACTIVE)
    {
#ifndef UVENT_ENABLE_REUSEADDR
        {
            uint64_t s = this->header_->state.load(std::memory_order_relaxed);
            for (;;)
            {
                if (s & usub::utils::sync::refc::CLOSED_MASK) break;

                const uint64_t cnt = (s & usub::utils::sync::refc::COUNT_MASK);
                if (cnt == usub::utils::sync::refc::COUNT_MASK) break;
                const uint64_t ns = (s & ~usub::utils::sync::refc::COUNT_MASK) | (cnt + 1);

                if (this->header_->state.compare_exchange_weak(s, ns, std::memory_order_acq_rel,
                                                               std::memory_order_relaxed))
                    break;
                cpu_relax();
            }
        }
#else
        {
            uint64_t& st = this->header_->state;

            if ((st & usub::utils::sync::refc::CLOSED_MASK) == 0)
            {
                const uint64_t cnt = st & usub::utils::sync::refc::COUNT_MASK;
                if (cnt != usub::utils::sync::refc::COUNT_MASK)
                {
                    st = (st & ~usub::utils::sync::refc::COUNT_MASK) |
                        ((cnt + 1) & usub::utils::sync::refc::COUNT_MASK);
                }
            }
        }
#endif
#if UVENT_DEBUG
        spdlog::debug("set_timeout_ms(win): {}", this->header_->get_counter());
#endif
        auto* timer = new utils::Timer(timeout, utils::TIMEOUT);
        timer->addFunction(detail::processSocketTimeout, this->header_);
        this->header_->timer_id = system::this_thread::detail::wh->addTimer(timer);
    }

    template <Proto p, Role r>
    void Socket<p, r>::destroy() noexcept
    {
        this->header_->close_for_new_refs();
        system::this_thread::detail::pl->removeEvent(this->header_, core::OperationType::ALL);
#ifndef UVENT_ENABLE_REUSEADDR
        system::this_thread::detail::g_qsbr.retire(static_cast<void*>(this->header_),
                                                   &delete_header);
#else
        system::this_thread::detail::q_sh->enqueue(this->header_);
#endif
    }

    template <Proto p, Role r>
    void Socket<p, r>::remove()
    {
        system::this_thread::detail::pl->removeEvent(this->header_, core::OperationType::ALL);
        this->header_->close_for_new_refs();
    }

    template <Proto p, Role r>
    std::expected<std::string, usub::utils::errors::SendError> Socket<p, r>::receive(
        size_t chunk_size, size_t maxSize)
    {
        std::string result;
        result.reserve(chunk_size * 2);

        size_t totalReceive{0};
        auto recv_loop =
            [&](auto&& recv_fn) -> std::expected<std::string, usub::utils::errors::SendError>
        {
            std::vector<char> buf(chunk_size);
            while (true)
            {
                int received = recv_fn(buf.data(), buf.size());
                if (received < 0)
                {
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK) break;
                    return std::unexpected(usub::utils::errors::SendError::RecvFailed);
                }
                if (received == 0) break;

                totalReceive += received;
                if (totalReceive >= maxSize) break;

                result.append(buf.data(), received);
                if (received < static_cast<int>(chunk_size)) break;
            }
            return result;
        };

        if constexpr (p == Proto::TCP)
        {
            return recv_loop(
                [&](char* b, size_t sz)
                {
                    return ::recv(this->header_->fd, b, static_cast<int>(sz), 0);
                });
        }

        try
        {
            return std::visit(
                [&](auto&& addr) -> std::expected<std::string, usub::utils::errors::SendError>
                {
                    using T = std::decay_t<decltype(addr)>;
                    int addr_len = sizeof(T);
                    return recv_loop(
                        [&](char* b, size_t sz)
                        {
                            return ::recvfrom(this->header_->fd,
                                              b,
                                              static_cast<int>(sz),
                                              0,
                                              reinterpret_cast<sockaddr*>(&addr),
                                              &addr_len);
                        });
                },
                this->address);
        }
        catch (const std::bad_variant_access&)
        {
            return std::unexpected(usub::utils::errors::SendError::InvalidAddressVariant);
        }
    }

    template <Proto p, Role r>
    client_addr_t Socket<p, r>::get_client_addr() const
        requires(p == Proto::TCP && r == Role::ACTIVE)
    {
        return this->address;
    }

    template <Proto p, Role r>
    client_addr_t Socket<p, r>::get_client_addr()
        requires(p == Proto::TCP && r == Role::ACTIVE)
    {
        return this->address;
    }

    template <Proto p, Role r>
    utils::net::IPV Socket<p, r>::get_client_ipv() const
        requires(p == Proto::TCP && r == Role::ACTIVE)
    {
        return this->ipv;
    }

    template <Proto p, Role r>
    size_t Socket<p, r>::send_aux(uint8_t* buf, size_t size)
    {
        if (this->header_->fd == INVALID_FD)
            return static_cast<size_t>(-1);

        if constexpr (p == Proto::TCP)
        {
            int r = ::send(this->header_->fd,
                           reinterpret_cast<const char*>(buf),
                           static_cast<int>(size),
                           0);
            return (r < 0) ? static_cast<size_t>(-1) : static_cast<size_t>(r);
        }

        try
        {
            return std::visit(
                [&](auto&& addr) -> size_t
                {
                    using T = std::decay_t<decltype(addr)>;
                    int addr_len = static_cast<int>(sizeof(T));
                    int r = ::sendto(this->header_->fd,
                                     reinterpret_cast<const char*>(buf),
                                     static_cast<int>(size),
                                     0,
                                     reinterpret_cast<sockaddr*>(&addr),
                                     addr_len);
                    return (r < 0) ? static_cast<size_t>(-1) : static_cast<size_t>(r);
                },
                this->address);
        }
        catch (const std::bad_variant_access&)
        {
            return static_cast<size_t>(-1);
        }
    }

} // namespace usub::uvent::net

#endif // SOCKETWINDOWS_H