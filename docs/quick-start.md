# Quick Start

This page demonstrates how to create a minimal TCP server with **uvent**.

---

## Echo-like TCP Server

```cpp
#include <uvent/Uvent.h>
#include <uvent/net/Socket.h>

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
        if (rdsz <= 0) {
            socket.shutdown();
            break;
        }
        auto buf = std::make_unique<uint8_t[]>(1024);
        size_t wrsz = co_await socket.async_write(
                const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(httpResponse.data())),
                httpResponse.size()
        );
        if (wrsz <= 0) {
            break;
        }
    }
    co_return;
}

// listening server
task::Awaitable<void> listeningCoro() {
    // bind to TCP port 45900
    auto acceptor = net::TCPServerSocket{"0.0.0.0", 45900};

    for (;;) {
        auto soc = co_await acceptor.async_accept();

        // spawn coroutine per client
        if (soc) system::co_spawn(clientCoro(std::move(soc.value())));
    }
}

int main() {
    // global settings. used to setup default timeout
    settings::timeout_duration_ms = 5000;

    // add server coroutine to global task queue
    system::co_spawn(listeningCoro());

    // run event loop with 4 worker threads
    usub::Uvent uvent(4);
    uvent.run();

    return 0;
}
```
