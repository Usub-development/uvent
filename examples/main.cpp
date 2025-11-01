#include "uvent/Uvent.h"

using namespace usub::uvent;

task::Awaitable<void> clientCoro(net::TCPClientSocket socket)
{
    static constexpr size_t max_read_size = 64 * 1024;
    utils::DynamicBuffer buffer;
    buffer.reserve(max_read_size);

    static const std::string_view httpResponse =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 20\r\n"
        "\r\n"
        "{\"status\":\"success\"}";

    socket.set_timeout_ms(5000);
    while (true)
    {
        buffer.clear();
        ssize_t rdsz = co_await socket.async_read(buffer, max_read_size);
        socket.update_timeout(5000);
#ifdef UVENT_DEBUG
        spdlog::info("Read size: {}", rdsz);
        std::string s(buffer.data(), buffer.data() + buffer.size());
        spdlog::info("Received string (raw): {}", s);
#endif
        if (rdsz <= 0)
        {
#ifdef UVENT_DEBUG
            spdlog::info("Client disconnected");
#endif
            socket.shutdown();
            break;
        }
        auto buf = std::make_unique<uint8_t[]>(1024);
        size_t wrsz = co_await socket.async_write(
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(httpResponse.data())),
            httpResponse.size()
        );
#ifdef UVENT_DEBUG
        spdlog::warn("Write size: {}", wrsz);
#endif
        if (wrsz <= 0)
        {
            break;
        }
        socket.update_timeout(5000);
    }
#ifdef UVENT_DEBUG
    spdlog::warn("client_coro finished");
#endif
    co_return;
}

task::Awaitable<void> test_coro()
{
    using namespace std::chrono_literals;
    std::cout << "test_coro()" << std::endl;
    co_await system::this_coroutine::sleep_for(2000ms);
    std::cout << "test_coro() 2" << std::endl;
    co_return;
}

task::Awaitable<void> listeningCoro()
{
    auto acceptor = new net::TCPServerSocket{"0.0.0.0", 45900};
    for (;;)
    {
        auto soc = co_await acceptor->async_accept();
        co_await test_coro();
        if (soc) system::co_spawn(clientCoro(std::move(soc.value())));
    }
}

task::Awaitable<void> sendingCoro()
{
#if UVENT_DEBUG
    spdlog::warn("sending coro");
#endif

    auto socket = net::TCPClientSocket{};

    auto res = co_await socket.async_connect("example.com", "80");
    if (res.has_value())
    {
#if UVENT_DEBUG
        spdlog::error("connect failed");
#endif
        co_return;
    }

#if UVENT_DEBUG
    spdlog::warn("connect success");
#endif

    uint8_t buffer[] =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: test-client\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n\r\n";

    size_t size = sizeof(buffer) - 1;

    auto result = co_await socket.async_send(buffer, size);
    if (!result.has_value())
    {
#if UVENT_DEBUG
        spdlog::warn("Failed async_send: {}", toString(result.error()));
#endif
        co_return;
    }

#if UVENT_DEBUG
    spdlog::warn("Success async_send: {} bytes", result.value());
#endif

    static constexpr size_t max_read_size = 64 * 1024;
    utils::DynamicBuffer read_buffer;
    read_buffer.reserve(max_read_size);

    while (true)
    {
        auto r = co_await socket.async_read(read_buffer, max_read_size);

#if UVENT_DEBUG
        spdlog::warn("async_read returned: {}", r);
#endif

        if (r <= 0)
            break;

        if (read_buffer.size() >= max_read_size)
            break;
    }

#if UVENT_DEBUG
    spdlog::warn(
        "RESPONSE BEGIN\n{}\nRESPONSE END",
        std::string(
            reinterpret_cast<const char*>(read_buffer.data()),
            read_buffer.size()
        )
    );
#endif

    co_return;
}

task::Awaitable<int, detail::AwaitableFrame<int>> generator()
{
    for (int i = 1; i <= 3; ++i)
    {
        std::cout << "yield " << i << "\n";
        co_yield i;
    }
    co_return 0;
}

task::Awaitable<void, detail::AwaitableFrame<void>> consumer()
{
    auto g = generator();

    while (true)
    {
        int v = co_await g;
        std::cout << "got " << v << "\n";

        if (g.get_promise()->get_coroutine_handle().done())
            break;
    }

    co_return;
}


int main()
{
    settings::timeout_duration_ms = 5000;
#ifdef UVENT_DEBUG
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v%$");
    spdlog::set_level(spdlog::level::trace);
#endif
    usub::Uvent uvent(4);
    uvent.for_each_thread([&](int threadIndex, thread::ThreadLocalStorage* tls)
    {
        system::co_spawn_static(listeningCoro(), threadIndex);
        system::co_spawn_static(listeningCoro(), threadIndex);
    });
    system::co_spawn(sendingCoro());
    system::co_spawn(consumer());
    uvent.run();
    return 0;
}
