# Uvent

Cross-platform async I/O engine with native backends for:

| OS / family                                    | Backend primitives                                                           | Implementation                                                  |
|------------------------------------------------|------------------------------------------------------------------------------|-----------------------------------------------------------------|
| Linux                                          | `epoll` (edge-triggered), non-blocking sockets / **`io_uring` (optional)**   | `SocketLinux`, `SocketLinuxIOUring`, `EPoller`, `IOUringPoller` |
| macOS, FreeBSD, OpenBSD, NetBSD, DragonFly BSD | `kqueue`, non-blocking sockets (`accept` + `fcntl`)                          | `SocketBSD`, `KQueuePoller`                                     |
| Windows 10+ / Windows Server 2016+             | **IOCP** (`WSARecv` / `WSASend` / `AcceptEx` / `ConnectEx` / `TransmitFile`) | `SocketWindows`, `IocpPoller`                                   |

A single high-level API (`TCPServerSocket`, `TCPClientSocket`, `UDPSocket`) is used across all platforms.

### Highlights

- **C++23 coroutines end to end** — `co_await` on accept/read/write/connect, no callbacks, no manual state machines.
- **Batch-draining accept** — `async_accept(handler)` drains the whole kernel backlog per wakeup and spawns a coroutine per connection (no lost edges in ET mode).
- **Async DNS** — `net::async_resolve` never blocks the event loop: IP literals resolve inline, hostnames go through a lazy worker pool.
- **Happy Eyeballs (RFC 8305)** — `net::connect_happy` races IPv6/IPv4 endpoints: parallel AAAA/A with resolution delay, staggered attempts, instant fallback on failure (early start).
- **Instant cross-thread wakeups** — parked pollers are signalled via `eventfd` / `EVFILT_USER` / `PostQueuedCompletionStatus`; resumes never wait for the idle tick.
- **Coroutine-native synchronization** — `AsyncMutex`, `AsyncSemaphore`, `AsyncEvent`, `WaitGroup`, `AsyncBarrier`, cancellation tokens; waiters suspend instead of blocking threads.
- **Go-style channels** — buffered MPMC `AsyncChannel<Ts...>` with back-pressure, unbounded `AsyncUnboundedChannel<Ts...>` for fire-and-forget producers (including non-runtime threads), and `select_recv` over multiple channels.
- **Timer wheel** — millions of cheap one-shot timers, coroutine `sleep_for`, per-socket inactivity timeouts.

### Requests per second (RPS)

| Threads | uvent   | Boost.Asio | libuv |
|---------|---------|------------|-------|
| 1       | 108,875 | 97,219     | 116   |
| 2       | 208,346 | 185,813    | 828   |
| 4       | 378,450 | 330,374    | 830   |
| 8       | 610,102 | 423,409    | 827   |

⚡ **Conclusion:** `uvent` delivers performance nearly on par with Boost.Asio and significantly outperforms libuv, while
keeping low latency (p99 around 2–3 ms).

👉 For more detailed and up-to-date benchmark results, see the dedicated
repository: [usub-foundation/io_perfomance](https://github.com/usub-foundation/io_perfomance)

# Quick start

Minimal TCP echo server:

```cpp
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
        if (rdsz <= 0)
        {
            socket.shutdown();
            break;
        }
        size_t wrsz = co_await socket.async_write(
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(httpResponse.data())),
            httpResponse.size());
        if (wrsz <= 0)
            break;
        socket.update_timeout(5000);
    }
    co_return;
}

task::Awaitable<void> listeningCoro()
{
    auto acceptor = new net::TCPServerSocket{"0.0.0.0", 45900};
    co_await acceptor->async_accept(clientCoro);
}

int main()
{
    settings::timeout_duration_ms = 5000;

    usub::Uvent uvent(4);
    uvent.for_each_thread([&](int threadIndex, thread::ThreadLocalStorage* tls) {
        system::co_spawn_static(listeningCoro(), threadIndex);
    });

    uvent.run();
    return 0;
}
```

`async_accept` accepts a coroutine function directly — no manual loop or `co_spawn` needed.
Each accepted connection is automatically spawned as a separate coroutine.

### Connecting out: Happy Eyeballs

Dual-stack clients shouldn't hang on a broken IPv6 path. `connect_happy`
implements RFC 8305: AAAA and A are resolved in parallel and the addresses are
raced — the first successful connect wins, a fast failure starts the next
address immediately:

```cpp
#include "uvent/net/HappyEyeballs.h"

task::Awaitable<void> fetch()
{
    auto res = co_await net::connect_happy("example.org", "443");
    if (!res)
        co_return; // res.error(): last ConnectError

    net::TCPClientSocket sock = std::move(*res);
    // sock.ipv says which family won; use async_write/async_read as usual
}
```

For a plain single-address connect, `TCPClientSocket::async_connect(host, port[, timeout])`
resolves asynchronously and never blocks the loop.

### Backend selection

Uvent automatically selects the best backend for your OS:

- **Linux** → `epoll` by default, or **io_uring** when explicitly enabled
- **Windows** → **IOCP** (always enabled, no flags required)
- **BSD / macOS** → `kqueue`

#### io_uring

To enable `io_uring` on Linux during build:

```bash
cmake -DUVENT_ENABLE_IO_URING=ON ..
make -j
```

or via CMake FetchContent:

```cmake
set(UVENT_ENABLE_IO_URING ON)
```

Requires Linux kernel **5.1+** and [liburing](https://github.com/axboe/liburing).

# Documentation

- [Getting started (installation)](https://usub-foundation.github.io/uvent/getting-started/)
- [Quick start](https://usub-foundation.github.io/uvent/quick-start/)
- [Tutorial (step-by-step tour of everything)](https://usub-foundation.github.io/uvent/tutorial/)
- [System primitives](https://usub-foundation.github.io/uvent/system_primitives/)
- [Settings](https://usub-foundation.github.io/uvent/settings/)
- [Awaitable](https://usub-foundation.github.io/uvent/awaitable/)
- [Awaitable frame](https://usub-foundation.github.io/uvent/awaitable_frame/)
- [Socket](https://usub-foundation.github.io/uvent/socket/)
- [Name Resolution & Happy Eyeballs](https://usub-foundation.github.io/uvent/resolver/)
- [Synchronization primitives & Channels](https://usub-foundation.github.io/uvent/synchronization/)

---

# Licence

Uvent is distributed under the [MIT license](LICENSE)