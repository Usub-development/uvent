# System Primitives

This section covers two core helpers used in user code:  
- `sleep_for` — suspend a coroutine for a given duration.  
- `co_spawn` — schedule a coroutine into the global task queue.

---

## sleep_for

Namespace: `usub::uvent::system::this_coroutine`

```cpp
template <typename Rep, typename Period>
task::Awaitable<void> sleep_for(const std::chrono::duration<Rep, Period>& duration);
```

Usage:

```cpp
using namespace std::chrono_literals;

task::Awaitable<void> worker() {
    // suspend coroutine for 1 second
    co_await system::this_coroutine::sleep_for(1s);

    // resume here after timeout
}
```

**What happens internally**:

* A `Timer` object is created and registered in the `TimerWheel`.
* The currently running coroutine handle is stored in the timer.
* When the timer expires, the coroutine is resumed.

This is the standard way to do coroutine-friendly sleeping instead of `std::this_thread::sleep_for`.

---

## co\_spawn

Namespace: `usub::uvent::system`

```cpp
template <typename F>
void co_spawn(F&& f);
```

Usage:

```cpp
task::Awaitable<void> client_handler() {
    // ...
}

int main() {
    // put coroutine into the global task queue
    system::co_spawn(client_handler());

    usub::Uvent loop(4);
    loop.run();
}
```

**What happens internally**:

* Extracts the coroutine’s promise via `get_promise()`.
* Enqueues its handle into the global shared task queue (`SharedTasks`).
* From there the event loop picks it up and starts execution.

This is the entry point for scheduling coroutines in **uvent** — any coroutine you want to run must be launched with `co_spawn`.