# Channel & Select Examples

This page contains practical examples of using `AsyncChannel` and `select_recv` in real coroutine-based applications.

---

# Basic Producer / Consumer

```cpp
AsyncChannel<int> ch{4};

task::Awaitable<void> producer() {
    for (int i = 0; i < 10; ++i) {
        co_await ch.send(i);
    }
    ch.close();
}

task::Awaitable<void> consumer() {
    for (;;) {
        auto opt = co_await ch.recv();
        if (!opt) break;
        auto& [v] = *opt;
        std::cout << "received: " << v << "\n";
    }
}
```

---

# Sending Structs or Multiple Fields

With multi-value channels:

```cpp
AsyncChannel<int, std::string> logCh{128};

co_await logCh.send(42, "hello");
```

Receiving side:

```cpp
auto opt = co_await logCh.recv();
auto& [code, text] = *opt;
```

---

# Using `operator<<`

```cpp
co_await (ch << 777);
co_await (multiCh << std::make_tuple(4, "data"));
```

---

# Using `try_send` / `try_recv`

```cpp
if (!ch.try_send(5)) {
    std::cout << "queue is full\n";
}

int v;
if (ch.try_recv_into(v)) {
    std::cout << "got: " << v << "\n";
}
```

---

# Select Example (Two Channels)

```cpp
AsyncChannel<int> ch1{4};
AsyncChannel<int> ch2{4};

task::Awaitable<void> selectLoop() {
    for (;;) {
        auto res = co_await select_recv(ch1, ch2);
        if (!res) {
            std::cout << "all done\n";
            co_return;
        }

        auto [idx, tup] = *res;
        auto& [v] = tup;

        std::cout << "received " << v << " from channel " << idx << "\n";
    }
}
```

---

# Fan-in Example (Three Channels)

```cpp
AsyncChannel<int> A{8}, B{8}, C{8};

task::Awaitable<void> merge() {
    for (;;) {
        auto r = co_await select_recv(A, B, C);
        if (!r) break;

        auto [index, tup] = *r;
        auto& [value] = tup;

        std::cout << "[merge] from " << index << ": " << value << "\n";
    }
}
```

---

# Pipeline Example

```cpp
AsyncChannel<int> input{64};
AsyncChannel<int> output{64};

task::Awaitable<void> stage1() {
    for (;;) {
        auto opt = co_await input.recv();
        if (!opt) { output.close(); co_return; }
        auto& [v] = *opt;
        co_await output.send(v * 2);
    }
}

task::Awaitable<void> stage2() {
    for (;;) {
        auto opt = co_await output.recv();
        if (!opt) co_return;
        auto& [v] = *opt;
        std::cout << "processed: " << v << "\n";
    }
}
```

---

# Closing Notes

* Channels are ideal for pipelines, merging, workers, schedulers, and message buses.
* `select_recv` enables advanced concurrent orchestration without locks.
* All operations integrate directly with the `uvent` runtime and coroutine scheduler.