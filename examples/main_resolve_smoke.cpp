#include "uvent/Uvent.h"
#include "uvent/net/Resolver.h"

#include <cstdio>
#include <cstdlib>

using namespace usub::uvent;

static int g_failures = 0;

static void report(const char* name, bool ok)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_failures;
}

task::Awaitable<void> echoHandler(net::TCPClientSocket socket)
{
    utils::DynamicBuffer buffer;
    buffer.reserve(1024);
    socket.set_timeout_ms(5000);
    ssize_t rdsz = co_await socket.async_read(buffer, 1024);
    if (rdsz > 0)
        co_await socket.async_write(const_cast<uint8_t*>(buffer.data()), buffer.size());
    socket.shutdown();
    co_return;
}

task::Awaitable<void> listenerCoro()
{
    auto acceptor = new net::TCPServerSocket{"127.0.0.1", 45907};
    for (;;)
        co_await acceptor->async_accept(echoHandler);
}

task::Awaitable<void> smokeCoro()
{
    using namespace std::chrono_literals;

    {
        net::TCPClientSocket s;
        auto res = co_await s.async_connect("127.0.0.1", "45907", 2000ms);
        report("connect 127.0.0.1 (numeric fast path)", !res.has_value());
    }

    {
        net::TCPClientSocket s;
        auto res = co_await s.async_connect("localhost", "45907", 2000ms);
        report("connect localhost (resolver pool)", !res.has_value());
    }

    {
        net::TCPClientSocket s;
        auto res = co_await s.async_connect("no-such-host.invalid", "80", 2000ms);
        report("connect no-such-host.invalid -> GetAddrInfoFailed",
               res.has_value() && *res == usub::utils::errors::ConnectError::GetAddrInfoFailed);
    }

    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        auto r = co_await net::async_resolve("localhost", "45907", hints);
        report("async_resolve localhost", r.has_value() && *r != nullptr);
        if (!r.has_value())
            std::printf("  gai error: %s\n", gai_strerror(r.error()));
    }

    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        constexpr int N = 20;
        int64_t total_us = 0, max_us = 0;
        bool all_ok = true;
        for (int i = 0; i < N; ++i)
        {
            auto t0 = std::chrono::steady_clock::now();
            auto r = co_await net::async_resolve("localhost", "45907", hints);
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
                          .count();
            all_ok = all_ok && r.has_value();
            total_us += us;
            max_us = std::max<int64_t>(max_us, us);
        }
        std::printf("  resolve resume latency: avg %.2f ms, max %.2f ms (N=%d)\n",
                    static_cast<double>(total_us) / N / 1000.0, static_cast<double>(max_us) / 1000.0, N);
        report("resolve resume latency avg < 20ms (poller wake)", all_ok && total_us / N < 20'000);
    }

    std::printf("resolve smoke: %s\n", g_failures == 0 ? "ALL PASS" : "FAILURES");
    std::exit(g_failures == 0 ? 0 : 1);
}

int main()
{
    usub::Uvent uvent(2);
    uvent.for_each_thread([&](int threadIndex, thread::ThreadLocalStorage*)
                          { system::co_spawn_static(listenerCoro(), threadIndex); });
    system::co_spawn(smokeCoro());
    uvent.run();
    return 0;
}
