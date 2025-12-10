# Async Channels

`AsyncChannel<Ts...>` is an asynchronous, coroutine-friendly communication primitive designed for high-performance message passing inside the **uvent** ecosystem.  
It provides back-pressured, buffered, multi-producer/multi-consumer communication without locking or manual coroutine management.

Channels behave similarly to Go channels while preserving full C++ performance and type safety.

---

## Key Features

- Lock-free MPMC ring buffer  
- Coroutine-awaitable `send()` and `recv()`  
- Fully non-blocking try-operations  
- Back-pressure when the buffer is full  
- Graceful `close()` semantics  
- Variadic message types (`AsyncChannel<Ts...>`)  
- Tuple-based messaging  
- Convenient `operator<<` syntax  
- Designed for massive concurrency and low latency

---

# Creating a Channel

```cpp
#include <uvent/sync/AsyncChannel.h>

AsyncChannel<int> ch{1024};
AsyncChannel<int, std::string> ch2{256};
```

The template parameters define the channel message payload.
Internally the channel always stores a `std::tuple<Ts...>`.

---

# Sending Values

## Awaitable Send

```cpp
co_await ch.send(42);
```

If the buffer is full, the coroutine suspends until space becomes available.

Return value:

* `true` — sent successfully
* `false` — channel was closed

### Operator `<<`

```cpp
co_await (ch << 42);
```

Multi-value example:

```cpp
AsyncChannel<int, std::string> c{64};
co_await (c << std::make_tuple(7, "hello"));
```

---

## Non-blocking Send

```cpp
bool ok = ch.try_send(123);
```

Returns `false` if the buffer is full.

---

# Receiving Values

## Awaitable Receive

```cpp
auto opt = co_await ch.recv();
if (!opt) {
    // channel closed and empty
}
auto& [value] = *opt;
```

Return value:

* `std::optional<std::tuple<Ts...>>`
* `nullopt` means the channel is closed **and empty**

---

## Receive into Variables

```cpp
int v;
bool ok = co_await ch.recv_into(v);
```

---

## Non-blocking Receive

```cpp
int v;
bool ok = ch.try_recv_into(v);
```

---

# Closing Channels

```cpp
ch.close();
```

Effects:

* All blocked senders/receivers are awakened
* Future sends fail with `false`
* Receivers drain buffered messages before returning `false` / `nullopt`

---

# Full Example

```cpp
AsyncChannel<int> ch{4};

task::Awaitable<void> producer() {
    for (int i = 0; i < 5; ++i)
        co_await ch.send(i);
    ch.close();
}

task::Awaitable<void> consumer() {
    for (;;) {
        auto opt = co_await ch.recv();
        if (!opt) break;
        auto& [v] = *opt;
        std::cout << "got: " << v << "\n";
    }
}
```

---

# Performance Notes

* No heap allocation during steady-state operation
* Lock-free MPMC queue for minimal contention
* Async waits rely on `AsyncEvent` without spinning
* Zero-copy tuple passing
* Ideal for pipelines, fan-in/out, and coroutine orchestration