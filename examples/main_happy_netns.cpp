#include "uvent/Uvent.h"
#include "uvent/net/HappyEyeballs.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace usub::uvent;

static const char* kV4 = "10.99.0.2";
static const char* kV6 = "fd00::2";
static const char* kPort = "45909";

task::Awaitable<void> echoHandler(net::TCPClientSocket socket)
{
    utils::DynamicBuffer buffer;
    buffer.reserve(64);
    socket.set_timeout_ms(5000);
    ssize_t rdsz = co_await socket.async_read(buffer, 64);
    if (rdsz > 0)
        co_await socket.async_write(const_cast<uint8_t*>(buffer.data()), buffer.size());
    socket.shutdown();
    co_return;
}

task::Awaitable<void> listenerCoro(const char* ip, utils::net::IPV ipv)
{
    net::TCPServerSocket* acceptor = nullptr;
    try
    {
        acceptor = new net::TCPServerSocket{std::string(ip), 45909, SOMAXCONN, ipv};
    }
    catch (const std::exception& e)
    {
        std::printf("[FAIL] listener %s bind/listen: %s\n", ip, e.what());
        std::fflush(stdout);
        std::_Exit(3);
    }
    std::printf("  listener %s up\n", ip);
    std::fflush(stdout);
    for (;;)
        co_await acceptor->async_accept(echoHandler);
}

task::Awaitable<void> smokeCoro(std::string scenario)
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    co_await system::this_coroutine::sleep_for(200ms);

    net::HappyEyeballsOptions opts;
    opts.attempt_delay = 250ms;
    opts.attempt_timeout = 3000ms;

    std::vector<net::ResolvedAddr> addrs;
    if (scenario == "local6")
    {
        addrs.push_back({"::1", utils::net::IPV6});
    }
    else
    {
        addrs.push_back({kV6, utils::net::IPV6});
        addrs.push_back({kV4, utils::net::IPV4});
    }

    auto t0 = steady_clock::now();
    auto res = co_await net::connect_happy_addrs(std::move(addrs), kPort, opts);
    auto el = duration_cast<milliseconds>(steady_clock::now() - t0);

    bool ok = false;
    if (scenario == "reject")
    {
        ok = res.has_value() && res->ipv == utils::net::IPV4 && el < 200ms;
    }
    else if (scenario == "drop")
    {
        ok = res.has_value() && res->ipv == utils::net::IPV4 && el >= 200ms && el < 1500ms;
    }
    else if (scenario == "v6live" || scenario == "local6")
    {
        ok = res.has_value() && res->ipv == utils::net::IPV6 && el < 200ms;
    }

    std::printf("[%s] happy netns %s: elapsed %lld ms, winner %s\n", ok ? "PASS" : "FAIL", scenario.c_str(),
                static_cast<long long>(el.count()),
                res.has_value() ? (res->ipv == utils::net::IPV6 ? "v6" : "v4") : "none");
    if (res.has_value())
        res->shutdown();
    std::fflush(stdout);
    std::_Exit(ok ? 0 : 1);
}

int main(int argc, char** argv)
{
    if (argc < 2 ||
        (std::strcmp(argv[1], "reject") && std::strcmp(argv[1], "drop") && std::strcmp(argv[1], "v6live") &&
         std::strcmp(argv[1], "local6")))
    {
        std::fprintf(stderr, "usage: %s reject|drop|v6live (внутри netns, см. run_happy_netns.sh) | local6 (хост)\n",
                     argv[0]);
        return 2;
    }
    std::string scenario = argv[1];

    usub::Uvent uvent(2);
    if (scenario == "local6")
        system::co_spawn(listenerCoro("::1", utils::net::IPV6));
    else
        system::co_spawn(listenerCoro(kV4, utils::net::IPV4));
    if (scenario == "v6live")
        system::co_spawn(listenerCoro(kV6, utils::net::IPV6));
    system::co_spawn(smokeCoro(std::move(scenario)));
    uvent.run();
    return 0;
}
