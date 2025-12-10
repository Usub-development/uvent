# Channel Select

`select_recv` allows a coroutine to wait for messages from **multiple channels at once**, similar to Go's `select`.  
This enables fan-in patterns, multiplexed consumers, and prioritizing fastest data sources.

---

# Overview

```cpp
auto res = co_await select_recv(ch1, ch2, ch3);
```

`select_recv` returns:

```cpp
std::optional<std::pair<size_t, value_type>>
```

Where:

* `size_t` — index of the channel that produced a message
* `value_type` — `std::tuple<Ts...>` associated with that channel

If **all channels are closed and empty**, the result is `nullopt`.

---

# Requirements

* All channels passed to `select_recv` must have the **same `value_type`**.
* Channels may have different capacities and producers.
* `select_recv` internally uses an event-based wake mechanism (`AsyncEvent`), not busy polling.

---

# Usage Example

```cpp
AsyncChannel<int> ch1{4};
AsyncChannel<int> ch2{4};

task::Awaitable<void> consumer() {
    for (;;) {
        auto res = co_await select_recv(ch1, ch2);
        if (!res) {
            std::cout << "all channels closed\n";
            co_return;
        }

        auto [idx, tup] = *res;
        auto& [v] = tup;

        std::cout << "received " << v << " from ch" << (idx + 1) << "\n";
    }
}
```

---

# Behavior Summary

### Ready value

If any channel already has data, `select_recv` returns immediately.

### Empty channels

If none have data, the coroutine suspends on a shared `AsyncEvent`.

### Wake-up

A wake-up occurs when:

* any channel receives a new message
* any channel is closed

### Closed channels

A channel contributes messages until its buffer is empty.
If **all channels are closed and empty**, the select returns `nullopt`.

---

# Example: Multiplexing Producers

```cpp
AsyncChannel<int> A{8};
AsyncChannel<int> B{8};

task::Awaitable<void> multiplexer() {
    for (;;) {
        auto r = co_await select_recv(A, B);
        if (!r) break;

        auto [index, tup] = *r;
        auto& [value] = tup;

        std::cout << "got " << value << " from channel " << index << "\n";
    }
}
```

---

# Notes

* `select_recv` does not guarantee fairness; it is optimized for throughput.
* Only receive-selection is supported (not send-selection).
* Ideal for fan-in patterns, pipeline aggregation, and merging asynchronous streams.