#ifndef UVENT_RESOLVER_H
#define UVENT_RESOLVER_H

#include "uvent/system/Defines.h"

#if defined(OS_WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace usub::uvent::net
{
    struct AddrInfoDeleter
    {
        void operator()(addrinfo* p) const noexcept
        {
            if (p)
                ::freeaddrinfo(p);
        }
    };

    /// Владеющая цепочка addrinfo: freeaddrinfo при разрушении.
    using AddrInfoPtr = std::unique_ptr<addrinfo, AddrInfoDeleter>;

    namespace detail
    {
        struct ResolveRequest
        {
            std::string host;
            std::string service;
            addrinfo hints{};
            addrinfo* result{nullptr};
            int rc{0};
            std::coroutine_handle<> waiter;
            int origin_tid{0};
        };

        class Resolver
        {
        public:
            static Resolver& instance();

            void submit(ResolveRequest* req);

            ~Resolver();

            Resolver(const Resolver&) = delete;
            Resolver& operator=(const Resolver&) = delete;

        private:
            Resolver();

            void workerLoop();

            std::mutex mtx_;
            std::condition_variable cv_;
            std::deque<ResolveRequest*> queue_;
            bool stop_{false};
            std::vector<std::thread> workers_;
        };
    } // namespace detail

    struct ResolveAwaiter
    {
        ResolveAwaiter(std::string host, std::string service, const addrinfo& hints);

        bool await_ready();
        void await_suspend(std::coroutine_handle<> h);
        std::expected<AddrInfoPtr, int> await_resume() noexcept;

    private:
        detail::ResolveRequest req_;
    };

    inline ResolveAwaiter async_resolve(std::string host, std::string service, const addrinfo& hints)
    {
        return ResolveAwaiter(std::move(host), std::move(service), hints);
    }
} // namespace usub::uvent::net

#endif // UVENT_RESOLVER_H
