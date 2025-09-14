# Uvent

High-performance asynchronous I/O library for C++23.

[![GitHub](https://img.shields.io/badge/GitHub-Usub--development%2Fuvent-blue?logo=github)](https://github.com/Usub-development/uvent)

---

## Features

- **C++23**: modern standard with coroutines and atomic primitives.  
- **Event loop**: non-blocking, scalable, based on `epoll/kqueue`.  
- **Sockets**: TCP/UDP (client and server).  
- **Timers**: custom `TimerWheel` with minimal overhead.  
- **Queues & synchronization**: lock-free data structures, QSBR, and reference counting.  

---

## Build & Installation

```bash
git clone https://github.com/Usub-development/uvent.git
cd uvent
mkdir build && cd build
cmake ..
make -j
```

## Quick example
```cpp
// simple echo-like server using uvent

task::Awaitable<void> clientCoro(net::TCPClientSocket socket) {
    utils::DynamicBuffer buf;
    buf.reserve(64 * 1024);

    // read from client
    ssize_t rdsz = co_await socket.async_read(buf, buf.capacity());

    // handle request / send response here ...
    co_return;
}

task::Awaitable<void> listeningCoro() {
    // listen on TCP port 45900
    auto acceptor = net::TCPServerSocket{"0.0.0.0", 45900};

    for (;;) {
        // accept new client
        auto soc = co_await acceptor.async_accept();

        // spawn client handler in event loop
        if (soc) system::co_spawn(clientCoro(std::move(soc.value())));
    }
}

int main() {
    // global settings
    settings::timeout_duration_ms = 5000;

    // add server coroutine to global task queue
    system::co_spawn(listeningCoro());

    // run event loop with 4 worker threads
    usub::Uvent uvent(4);
    uvent.run();

    return 0;
}
```
