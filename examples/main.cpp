#include "include/uvent/Uvent.h"
#include "include/uvent/tasks/AwaitableFrame.h"
#include "include/uvent/system/SystemContext.h"
#include "include/uvent/net/Socket.h"

using namespace usub::uvent;

task::Awaitable<void> clientCoro(net::TCPClientSocket socket) {
    static constexpr size_t max_read_size = 64 * 1024;
    utils::DynamicBuffer buffer;
    buffer.reserve(max_read_size);

    static const std::string_view httpResponse =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 20\r\n"
            "\r\n"
            "{\"status\":\"success\"}";
    while (true) {
        buffer.clear();
        ssize_t rdsz = co_await socket.async_read(buffer, max_read_size);
        socket.update_timeout(20000);
#ifdef UVENT_DEBUG
        spdlog::info("Read size: {}", rdsz);
        std::string s(buffer.data(), buffer.data() + buffer.size());
        spdlog::info("Received string (raw): {}", s);
#endif
        if (rdsz <= 0) {
#ifdef UVENT_DEBUG
            spdlog::info("Client disconnected");
#endif
            socket.shutdown();
            break;
        }
        auto buf = std::make_unique<uint8_t[]>(1024);
        size_t wrsz = co_await socket.async_write(
                const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(httpResponse.data())),
                httpResponse.size()
        );
        socket.update_timeout(20000);
        if (wrsz <= 0) {
            break;
        }
#ifdef UVENT_DEBUG
        spdlog::info("Write size: {}", wrsz);
#endif
    }
#ifdef UVENT_DEBUG
    spdlog::warn("client_coro finished");
#endif
    std::cout << "finished client coro\n";
    co_return;
}

task::Awaitable<void> listeningCoro() {
    auto acceptor = new net::TCPServerSocket{"0.0.0.0", 45900};
    for (;;) {
        auto soc = co_await acceptor->async_accept();
        if (soc) system::co_spawn(clientCoro(std::move(soc.value())));
    }
}

task::Awaitable<void> sendingCoro() {
    auto socket = net::TCPClientSocket{};
    auto res = co_await socket.async_connect("example.com", "80");
    if (res.has_value()) co_return;
    uint8_t buffer[] =
            "GET / HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "User-Agent: test-client\r\n"
            "Accept: */*\r\n"
            "Connection: close\r\n\r\n";
    size_t size = sizeof(buffer) - 1;

    for (int i = 0; i < 2; i++) {
        auto result = co_await socket.async_send(buffer, size);
        if (result.has_value()) {
            std::cout << result.value() << std::endl;
        } else {
            std::cout << toString(result.error()) << std::endl;
        }
    }

    co_return;
}

int main() {
    settings::timeout_duration_ms = 20000;
#ifdef UVENT_DEBUG
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v%$");
    spdlog::set_level(spdlog::level::trace);
#endif
    system::co_spawn(std::move(listeningCoro()));
    usub::Uvent uvent(4);
    uvent.run();
    return 0;
}