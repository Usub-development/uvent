# Synchronization Primitives

`uvent::sync` provides coroutine-native primitives with zero thread blocking, no heap allocations on the fast path, and
fairness suitable for high-concurrency runtimes.

Primitives:

- [`AsyncMutex`](#asyncmutex)
- [`AsyncSemaphore`](#asyncsemaphore)
- [`AsyncEvent`](#asyncevent)
- [`WaitGroup`](#waitgroup)
- [`CancellationSource` / `CancellationToken`](#cancellationsource--cancellationtoken)
- [`AsyncBarrier`](#asyncbarrier)

All operations suspend coroutines and re-schedule them through the event-loop queue (`system::this_thread::detail::q`)
instead of blocking OS threads.

---

## AsyncMutex

`AsyncMutex` is a coroutine-safe synchronization primitive designed for **Uvent**’s asynchronous runtime.  
It allows multiple coroutines to coordinate access to shared resources **without blocking threads**.

### Overview

Unlike traditional `std::mutex`, `AsyncMutex` suspends waiting coroutines and re-schedules them via the event loop
queue. This avoids kernel-level blocking and keeps CPUs busy with useful work.

### Features

- Fully coroutine-aware (`co_await mutex.lock()`).
- Zero blocking — waiters suspend.
- Single atomic with embedded waiter stack.
- Fair and efficient LIFO hand-off.
- Minimal memory footprint — no allocations.
- Guard-based RAII unlocking.

### Example

```cpp
#include "uvent/sync/AsyncMutex.h"
#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"
#include <iostream>

using namespace usub::uvent;
using usub::uvent::sync::AsyncMutex;

static AsyncMutex g_mutex;

task::Awaitable<void> worker(int id)
{
    {
        auto guard = co_await g_mutex.lock();
        std::cout << "Worker " << id << " acquired lock\n";
        co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(300));
        std::cout << "Worker " << id << " released lock\n";
    }
    co_return;
}
```

### API Reference

```cpp
namespace usub::uvent::sync {

class AsyncMutex {
public:
    struct Guard {
        bool owns_lock() const noexcept;
        void unlock() noexcept;
    };

    struct LockAwaiter {
        bool await_ready() noexcept;
        bool await_suspend(std::coroutine_handle<> h) noexcept;
        Guard await_resume() noexcept;
    };

    LockAwaiter lock() noexcept;
    Guard try_lock() noexcept;
    void unlock() noexcept;
};

}
```

### Internal Design

* Single `std::atomic<std::uintptr_t>` encodes state and waiter stack:

    * `0` → unlocked
    * `1` → locked, no waiters
    * `ptr|1` → locked, waiter stack head
* Waiters form an intrusive LIFO list on coroutine frames.
* Handoff enqueues the next coroutine on the runtime queue.

### Performance

| Scenario                    | Latency (uncontended) | Notes         |
|-----------------------------|-----------------------|---------------|
| Lock/Unlock (no contention) | ~30–50 ns             | Single CAS    |
| Handoff with contention     | ~120–150 ns           | CAS + enqueue |

### Summary

Use `AsyncMutex` for exclusive access in coroutine-heavy code without blocking threads.

---

## AsyncSemaphore

A coroutine-friendly counting semaphore controlling access to a fixed number of permits.

### Overview

Provides bounded parallelism: only `N` coroutines can proceed concurrently; the rest suspend and are resumed through the
event loop.

### Features

* `co_await sem.acquire()` for permit acquisition.
* `try_acquire()` non-suspending fast path.
* `release(k)` wakes waiters or returns permits to the counter.
* No heap allocations on the waiting path.

### Example

```cpp
#include "uvent/sync/AsyncSemaphore.h"
#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"

using namespace usub::uvent;
using usub::uvent::sync::AsyncSemaphore;

static AsyncSemaphore g_sem{2};

task::Awaitable<void> task_fn(int id)
{
    co_await g_sem.acquire();
    std::cout << "task " << id << " in\n";
    co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(300));
    std::cout << "task " << id << " out\n";
    g_sem.release();
    co_return;
}
```

### API Reference

```cpp
namespace usub::uvent::sync {

class AsyncSemaphore {
public:
    explicit AsyncSemaphore(int32_t initial) noexcept;

    struct AcquireAwaiter {
        bool await_ready() noexcept;
        bool await_suspend(std::coroutine_handle<> h) noexcept;
        void await_resume() noexcept;
    };

    AcquireAwaiter acquire() noexcept;
    bool try_acquire() noexcept;
    void release(int32_t count = 1) noexcept;
};

}
```

### Internal Design

* Atomic `count` plus intrusive waiter stack.
* Acquire fast path decrements `count` with CAS; otherwise enqueues waiter.
* Release pops waiter (handoff) or increments `count` if none present.

### Performance

| Scenario        | Latency (permit available) | Notes         |
|-----------------|----------------------------|---------------|
| Acquire/Release | ~30–60 ns                  | Single CAS    |
| Wake waiter     | ~120–160 ns                | CAS + enqueue |

### Summary

Use `AsyncSemaphore` to cap concurrency for I/O, pools, or CPU-bound sections.

---

## AsyncEvent

Coroutine-aware event with **Auto** (wake one) and **Manual** (wake all) reset modes.

### Overview

Waiters suspend on `wait()`. `set()` wakes one or all waiters depending on the mode. Manual mode stays signaled until
`reset()`.

### Features

* `Reset::Auto` behaves like a futex wake-one.
* `Reset::Manual` behaves like a broadcast barrier.
* `wait()` is an awaitable; no thread blocking.

### Example

```cpp
#include "uvent/sync/AsyncEvent.h"
#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"

using namespace usub::uvent;
using usub::uvent::sync::AsyncEvent;
using usub::uvent::sync::Reset;

static AsyncEvent g_evt{Reset::Manual, false};

task::Awaitable<void> waiter(int id)
{
    std::cout << "waiter " << id << " waiting\n";
    co_await g_evt.wait();
    std::cout << "waiter " << id << " woke\n";
    co_return;
}

task::Awaitable<void> trigger()
{
    co_await system::this_coroutine::sleep_for(std::chrono::seconds(1));
    g_evt.set();
    co_return;
}
```

### API Reference

```cpp
namespace usub::uvent::sync {

enum class Reset { Auto, Manual };

class AsyncEvent {
public:
    explicit AsyncEvent(Reset mode = Reset::Auto, bool set = false) noexcept;

    struct WaitAwaiter {
        bool await_ready() noexcept;
        bool await_suspend(std::coroutine_handle<> h) noexcept;
        void await_resume() noexcept;
    };

    WaitAwaiter wait() noexcept;
    void set() noexcept;
    void reset() noexcept;
};

}
```

### Internal Design

* Atomic `set` flag plus intrusive waiter stack.
* Auto-reset: `set()` wakes a single waiter and clears the flag.
* Manual-reset: `set()` wakes all waiters and keeps the flag set.

### Performance

| Scenario    | Latency     | Notes              |
|-------------|-------------|--------------------|
| Wait ready  | ~10–20 ns   | Flag read/CAS      |
| set() wake1 | ~100–140 ns | Pop + enqueue      |
| set() wakeN | O(N)        | Linear resume cost |

### Summary

Use `AsyncEvent` for signaling readiness or state transitions between coroutines.

---

## WaitGroup

A barrier primitive to wait for a group of coroutines to finish, similar to Go’s `sync.WaitGroup`.

### Overview

Call `add(N)` before spawning `N` tasks. Each task calls `done()` once when finished. Await `wait()` to resume when the
internal counter reaches zero.

### Features

* Zero blocking; `wait()` suspends the awaiting coroutine.
* Multiple concurrent waiters supported.
* No allocations; intrusive waiter list.

### Example

```cpp
#include "uvent/sync/WaitGroup.h"
#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"

using namespace usub::uvent;
using usub::uvent::sync::WaitGroup;

static WaitGroup g_wg;

task::Awaitable<void> unit(int id)
{
    co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(200));
    g_wg.done();
    co_return;
}

task::Awaitable<void> controller()
{
    g_wg.add(3);
    system::co_spawn(unit(1));
    system::co_spawn(unit(2));
    system::co_spawn(unit(3));
    co_await g_wg.wait();
    co_return;
}
```

### API Reference

```cpp
namespace usub::uvent::sync {

class WaitGroup {
public:
    void add(int count) noexcept;
    void done() noexcept;

    struct Awaiter {
        bool await_ready() noexcept;
        bool await_suspend(std::coroutine_handle<> h) noexcept;
        void await_resume() noexcept;
    };

    Awaiter wait() noexcept;
};

}
```

### Internal Design

* Atomic counter plus waiter stack.
* `done()` decrements; when it hits zero, all waiters are resumed.

### Performance

| Scenario     | Latency   | Notes          |
|--------------|-----------|----------------|
| add/done     | ~10–25 ns | Atomic inc/dec |
| wake waiters | O(N)      | Resume each    |

### Summary

Use `WaitGroup` to join batches of coroutines without building ad-hoc barriers.

---

## CancellationSource / CancellationToken

Lightweight cooperative cancellation for coroutines.

### Overview

`CancellationSource` emits cancellation; `CancellationToken` is passed to tasks. Tasks periodically check
`stop_requested()` or await `on_cancel()` to react.

### Features

* Zero blocking; cancellation is cooperative.
* Any number of coroutines can share the same token.
* Immediate wake of all `on_cancel()` awaiters.

### Example

```cpp
#include "uvent/sync/Cancellation.h"
#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"

using namespace usub::uvent;
using usub::uvent::sync::CancellationSource;
using usub::uvent::sync::CancellationToken;

static CancellationSource g_src;

task::Awaitable<void> cancellable(CancellationToken tok)
{
    while (!tok.stop_requested())
        co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(200));
    co_return;
}

task::Awaitable<void> demo_cancel()
{
    auto tok = g_src.token();
    system::co_spawn(cancellable(tok));
    co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(1500));
    g_src.request_cancel();
    co_return;
}
```

### API Reference

```cpp
namespace usub::uvent::sync {

class CancellationToken {
public:
    bool stop_requested() const noexcept;

    struct Awaiter {
        bool await_ready() noexcept;
        bool await_suspend(std::coroutine_handle<> h) noexcept;
        void await_resume() noexcept;
    };

    Awaiter on_cancel() const noexcept;
};

class CancellationSource {
public:
    CancellationToken token() noexcept;
    void request_cancel() noexcept;
};

}
```

### Internal Design

* Atomic `requested` flag with intrusive waiter list.
* `request_cancel()` flips the flag and resumes all registered waiters at once.

### Performance

| Scenario         | Latency  | Notes          |
|------------------|----------|----------------|
| stop_requested() | ~5–10 ns | Atomic load    |
| request_cancel() | O(N)     | Resume waiters |

### Summary

Use cancellation to terminate long-running coroutines, enforce deadlines, or compose `with_timeout()`-style utilities.

---

## AsyncBarrier

A coroutine-native **cyclic barrier** (similar to `std::barrier`) that synchronizes a fixed number of participants
without blocking threads.

`AsyncBarrier` is useful when you need **phase-based coordination** between coroutines running on multiple runtime
threads: all participants must reach the barrier before any of them may continue.

### Overview

Each call to `arrive_and_wait()` suspends the current coroutine until the required number of participants has arrived.
When the last participant arrives, the barrier releases **all waiters of the current phase** and automatically resets
for the next phase.

Unlike an event-style primitive, a barrier provides **collective progress**: no participant can pass early.

### Features

* Cyclic / reusable (phase-based synchronization).
* Zero OS thread blocking — only coroutine suspension.
* No heap allocations on the fast path (waiters are stored intrusively in coroutine frames).
* Wake-up is performed by **re-scheduling coroutine handles into the target threads’ queues**.
* Correct across threads: each resumed coroutine is enqueued to its owning runtime thread using
  `promise.get_thread_id()`.

### Example (startup barrier across threads)

```cpp
#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"
#include <iostream>

using namespace usub::uvent;

static sync::AsyncBarrier g_barrier{4};

task::Awaitable<void> worker(int id)
{
    std::cout << "worker " << id << " phase0\n";
    co_await g_barrier.arrive_and_wait();
    std::cout << "worker " << id << " phase1\n";
    co_return;
}

int main()
{
    usub::Uvent uvent(4);

    uvent.for_each_thread([&](int tid, thread::ThreadLocalStorage*)
    {
        system::co_spawn_static(worker(tid), tid);
    });

    uvent.run();
}
```

### API Reference

```cpp
namespace usub::uvent::sync {
    class AsyncBarrier {
    public:
        explicit AsyncBarrier(std::size_t parties) noexcept;

        struct Awaiter {
            bool await_ready() noexcept;
            template<class Promise>
            bool await_suspend(std::coroutine_handle<Promise> h) noexcept;
            void await_resume() noexcept;
        };

        Awaiter arrive_and_wait() noexcept;
    };
}
```

### Semantics

`arrive_and_wait()`:

* If this coroutine is **not** the last to arrive in the current phase:

    * The coroutine **suspends** and is placed into the barrier’s waiter list.
* If this coroutine is the **last** to arrive:

    * The barrier atomically starts the next phase and **releases all waiters** from the current phase.

### Internal Design

* Barrier stores:

    * `parties` — required number of participants.
    * `arrived` — number of arrivals in the current phase.
    * Intrusive singly-linked list of waiter nodes embedded into suspended coroutine frames.
* The last arriving coroutine drains the waiter list and re-schedules each waiter by pushing its coroutine handle into
  the correct runtime thread queue.
* Thread affinity is preserved by reading the owner thread id from the awaiting coroutine promise:
  `h.promise().get_thread_id()`.
* Re-scheduling is performed by enqueuing coroutine handles to the target thread inbox before startup
  (`system::co_spawn_static(handle, tid)`) or to the event-loop queue after startup (implementation-specific).

### Performance

| Scenario                   | Latency (typical) | Notes                 |
|----------------------------|-------------------|-----------------------|
| arrive_and_wait (not last) | ~30–80 ns         | bookkeeping + suspend |
| arrive_and_wait (last)     | O(N)              | enqueue each waiter   |

### Summary

Use `AsyncBarrier` when multiple coroutines must advance in lockstep across phases, especially in multi-threaded
event-loop setups.