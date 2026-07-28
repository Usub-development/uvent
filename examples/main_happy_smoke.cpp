#include "uvent/Uvent.h"
#include "uvent/net/HappyEyeballs.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace usub::uvent;

static int g_failures = 0;

static void report(const char* name, bool ok)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    std::fflush(stdout);
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
    auto acceptor = new net::TCPServerSocket{"127.0.0.1", 45908};
    for (;;)
        co_await acceptor->async_accept(echoHandler);
}

task::Awaitable<bool> checkEcho(net::TCPClientSocket& sock)
{
    uint8_t msg[] = "ping";
    auto w = co_await sock.async_send(msg, 4);
    if (!w.has_value())
        co_return false;
    utils::DynamicBuffer buf;
    buf.reserve(64);
    ssize_t r = co_await sock.async_read(buf, 64);
    co_return r == 4;
}

task::Awaitable<void> smokeCoro()
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    {
        auto res = co_await net::connect_happy("localhost", "45908");
        bool ok = res.has_value();
        if (ok)
        {
            bool echo = co_await checkEcho(*res);
            ok = echo;
            res->shutdown();
        }
        report("connect_happy localhost + echo", ok);
    }

    {
        auto t0 = steady_clock::now();
        net::HappyEyeballsOptions opts;
        opts.attempt_delay = 250ms;
        opts.attempt_timeout = 5000ms;
        std::vector<net::ResolvedAddr> addrs;
        addrs.push_back({"127.0.0.2", utils::net::IPV4}); // RST: листенера нет
        addrs.push_back({"127.0.0.1", utils::net::IPV4});
        auto res = co_await net::connect_happy_addrs(std::move(addrs), "45908", opts);
        auto el = duration_cast<milliseconds>(steady_clock::now() - t0);
        bool ok = res.has_value() && el < 200ms;
        bool echo = false;
        if (res.has_value())
        {
            echo = co_await checkEcho(*res);
            ok = ok && echo;
            res->shutdown();
        }
        std::printf("  race elapsed: %lld ms (res=%d echo=%d)\n", static_cast<long long>(el.count()),
                    res.has_value() ? 1 : 0, echo ? 1 : 0);
        report("race RST-first vs live-second (early-start, no stagger wait)", ok);
    }

    {
        auto t0 = steady_clock::now();
        net::HappyEyeballsOptions opts;
        opts.attempt_delay = 400ms;
        opts.attempt_timeout = 5000ms;
        std::vector<net::ResolvedAddr> addrs;
        addrs.push_back({"127.0.0.1", utils::net::IPV4});
        addrs.push_back({"127.0.0.2", utils::net::IPV4});
        auto res = co_await net::connect_happy_addrs(std::move(addrs), "45908", opts);
        auto el = duration_cast<milliseconds>(steady_clock::now() - t0);
        bool ok = res.has_value() && el <= 200ms;
        if (res.has_value())
            res->shutdown();
        std::printf("  early-win elapsed: %lld ms\n", static_cast<long long>(el.count()));
        report("instant winner, no stagger wait", ok);
    }

    {
        auto t0 = steady_clock::now();
        net::HappyEyeballsOptions opts;
        opts.attempt_delay = 100ms;
        opts.attempt_timeout = 700ms;
        std::vector<net::ResolvedAddr> addrs;
        addrs.push_back({"127.0.0.2", utils::net::IPV4});
        addrs.push_back({"127.0.0.2", utils::net::IPV4});
        auto res = co_await net::connect_happy_addrs(std::move(addrs), "45908", opts);
        auto el = duration_cast<milliseconds>(steady_clock::now() - t0);
        std::printf("  all-fail elapsed: %lld ms\n", static_cast<long long>(el.count()));
        report("all-dead -> error once attempts exhausted",
               !res.has_value() && el <= 2500ms);
    }

    {
        auto res = co_await net::connect_happy("no-such-host.invalid", "80");
        report("connect_happy no-such-host.invalid -> GetAddrInfoFailed",
               !res.has_value() && res.error() == usub::utils::errors::ConnectError::GetAddrInfoFailed);
    }

    std::printf("happy eyeballs smoke: %s\n", g_failures == 0 ? "ALL PASS" : "FAILURES");
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
