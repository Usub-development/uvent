# System Primitives

Core coroutine and timer utilities available under `usub::uvent::system`.

These primitives handle coroutine scheduling, thread targeting, and timed suspension inside the uvent runtime.

---

## sleep_for

Namespace: `usub::uvent::system::this_coroutine`

```cpp
template <typename Rep, typename Period>
task::Awaitable<void> sleep_for(const std::chrono::duration<Rep, Period>& duration);
````

Suspends the current coroutine for the given duration using the internal `TimerWheel`.
Unlike `std::this_thread::sleep_for`, it doesn’t block a thread — it simply parks the coroutine and resumes it once the timer expires.

### Example

```cpp
using namespace std::chrono_literals;

task::Awaitable<void> taskA() {
    co_await system::this_coroutine::sleep_for(500ms);
    std::cout << "resumed after 500ms\n";
}
```

### Behavior

* Creates a `utils::Timer` with `TimerType::TIMEOUT`.
* Binds the currently executing coroutine to the timer.
* Adds the timer into the thread-local `TimerWheel`.
* The coroutine resumes automatically once the timer expires.

### Notes

* The timer object is automatically managed by the runtime.
* Safe to use in any coroutine running within a valid `uvent` thread context.

---

## co_spawn

Namespace: `usub::uvent::system`

```cpp
template <typename F>
void co_spawn(F&& f);
```

Schedules a coroutine for execution in the global thread context.

### Example

```cpp
task::Awaitable<void> client() {
    // coroutine logic
    co_return;
}

void run() {
    system::co_spawn(client());
}
```

### Behavior

* Retrieves the coroutine’s promise via `get_promise()`.
* Enqueues its coroutine handle into the shared global task queue (`SharedTasks`).
* Once a worker thread picks it up, execution begins.

### Notes

* Use this only after the event loop has started.
* If you need to queue coroutines before startup, use `co_spawn_static()`.

---

## co_spawn_static

Namespace: `usub::uvent::system`

```cpp
template <typename F>
void co_spawn_static(F&& f, int threadIndex);
```

Enqueues a coroutine into a specific thread’s inbox **before** the event loop starts.
Useful for registering startup coroutines that must run on a fixed thread.

### Example

```cpp
task::Awaitable<void> startup() {
    // initialization coroutine
    co_return;
}

int main() {
    // schedule startup coroutine for thread #0
    system::co_spawn_static(startup(), 0);

    usub::Uvent loop(4);
    loop.run();
}
```

### Behavior

* Retrieves the coroutine handle via its promise.
* Pushes the handle into the inbox queue of the target thread (via `TLSRegistry`).
* Throws `std::runtime_error` if the event loop is already running.

### Notes

* Must be called **only before** `uvent` starts.
* Designed for pre-loop scheduling, initialization, or per-thread setup logic.

---

## spawn_timer

Namespace: `usub::uvent::system`

```cpp
inline void spawn_timer(utils::Timer* timer);
```

Adds a raw timer instance into the thread-local timer wheel.

### Example

```cpp
auto* t = new utils::Timer(2000, utils::TimerType::TIMEOUT);

// capture arbitrary data using std::any
std::any payload = std::string("timer fired");

t->addFunction([](std::any& arg) {
    auto& msg = std::any_cast<std::string&>(arg);
    std::cout << msg << std::endl;
}, payload);

system::spawn_timer(t);
```

### Behavior

* Inserts the provided timer into the timer wheel of the current thread.
* Once its expiry is reached, its callback or bound coroutine is invoked.

### Notes

* The timer must be properly constructed and inactive before calling.
* Supports both one-shot (`TIMEOUT`) and interval (`INTERVAL`) modes.
* Does not perform ownership checks — memory management is the caller’s responsibility.

---

## Summary

| Function                          | Purpose                                              | Context          |
| --------------------------------- | ---------------------------------------------------- | ---------------- |
| `sleep_for(duration)`             | Suspend coroutine for the specified time             | Coroutine        |
| `co_spawn(f)`                     | Schedule coroutine in global task queue              | Runtime running  |
| `co_spawn_static(f, threadIndex)` | Queue coroutine for a specific thread before startup | Pre-runtime      |
| `spawn_timer(timer)`              | Register custom timer for execution                  | Timer management |

These primitives form the low-level foundation of **uvent’s coroutine runtime**, allowing safe, event-driven execution and timed suspension within the thread pool.