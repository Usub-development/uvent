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

Every blocking operation is **cancellation-aware**: when the task that awaits it is cancelled, the wait aborts and the
operation reports failure (`false`, `std::nullopt`, or an empty `Guard`) instead of the acquired resource — see
[Cancellation](cancellation.md). Waiters register in per-primitive intrusive lists embedded in the coroutine frame
(spinlock-protected, no heap allocation per wait), which is also what makes them usable as branches of
[`sync::select`](channels/select.md).

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
        Guard await_resume() noexcept;   // empty Guard when the task was cancelled while queued
    };

    LockAwaiter lock() noexcept;
    Guard try_lock() noexcept;
    void unlock() noexcept;
};

}
```

`co_await mtx.lock()` returns a `Guard`; always check `owns_lock()` in cancellable code — a cancelled waiter is removed
from the queue and receives an empty guard.

### Internal Design

* `std::atomic<uint32_t>` lock word; the uncontended path is a single CAS.
* Contended waiters queue in an intrusive FIFO `WaitList` (nodes live in the coroutine frame; the list spinlock is held
  only for push/pop/remove).
* `unlock()` hands the lock directly to the first queued waiter (no barging window) and resumes it **on the worker that
  parked it**; with no waiters it simply stores 0.
* Cancellation unlinks the waiter under the same lock, so a cancelled coroutine can never be resumed twice.

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

    task::Awaitable<bool> acquire() noexcept;   // false => cancelled, no permit taken
    bool try_acquire() noexcept;
    void release(int32_t count = 1) noexcept;
    int32_t available() const noexcept;
    AcquireOp acquire_op() noexcept;            // select() branch
};

}
```

### Internal Design

* Atomic `count`; the fast path is a single CAS, contended waiters queue in a `WaitList`.
* `release(k)` adds the permits, then wakes up to `k` queued waiters; woken waiters re-run the CAS (level-triggered), so
  a permit consumed by a faster `try_acquire` simply re-parks the waiter.
* Cancellation unlinks the waiter; `co_await acquire()` then returns `false` and no permit is held.

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

    task::Awaitable<bool> wait() noexcept;      // false => cancelled
    bool try_consume() noexcept;
    bool is_set() const noexcept;
    void set() noexcept;
    void reset() noexcept;
    WaitOp wait_op() noexcept;                  // select() branch
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

    int count() const noexcept;

    task::Awaitable<bool> wait() noexcept;      // false => cancelled
    WaitOp wait_op() noexcept;                  // select() branch
};

}
```

### Internal Design

* Atomic counter plus an intrusive `WaitList`.
* `done()` decrements; when it hits zero, all waiters are resumed. Multiple concurrent waiters are all woken (the old
  semaphore-based version woke only one).

### Performance

| Scenario     | Latency   | Notes          |
|--------------|-----------|----------------|
| add/done     | ~10–25 ns | Atomic inc/dec |
| wake waiters | O(N)      | Resume each    |

### Summary

Use `WaitGroup` to join batches of coroutines without building ad-hoc barriers.

---

## CancellationSource / CancellationToken

Hierarchical cooperative cancellation. This section is a short reference; the full model (trees, task integration,
which operations react and how) lives in [Cancellation](cancellation.md).

### Overview

`CancellationSource` owns a node in a cancellation tree; `CancellationToken` is a cheap ref-counted handle observing
it. Cancelling a source marks its whole subtree: child sources created from a token, and every task spawned under it
(see [Tasks](tasks.md)).

### Features

* Zero blocking; cancellation is cooperative and never destroys a running coroutine.
* Tokens are copyable and may outlive their source.
* Tree propagation: `CancellationSource child{parent.token()}`.
* `on_cancel()` wakes immediately on `request_cancel()`, from any thread.
* Usable as a `select` branch via `on_cancel_op()`.

### Example

```cpp
#include "uvent/sync/AsyncCancellation.h"
#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"

using namespace usub::uvent;
using usub::uvent::sync::CancellationSource;
using usub::uvent::sync::CancellationToken;

static CancellationSource g_src;

task::Awaitable<void> cancellable(CancellationToken tok)
{
    while (!tok.stop_requested())
    {
        if (!co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(200)))
            break;
    }
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
    bool valid() const noexcept;
    bool stop_requested() const noexcept;

    OnCancelAwaiter on_cancel() const noexcept;   // co_await -> bool (false: own task cancelled first)
    CancelOp on_cancel_op() const noexcept;       // select() branch
};

class CancellationSource {
public:
    CancellationSource();                                   // root
    explicit CancellationSource(const CancellationToken&);  // child of an existing token
    CancellationToken token() const noexcept;
    void request_cancel() noexcept;
};

CancellationToken current_token() noexcept;                 // token of the running task

}
```

### Internal Design

* Ref-counted `CancelState` nodes linked into a tree (spinlock-guarded child lists, touched only on
  create/destroy/cancel).
* `request_cancel()` flips the flag, wakes `on_cancel()` waiters, kicks every task in the subtree, and recurses into
  child sources.
* `stop_requested()` is a single relaxed atomic load.

### Performance

| Scenario         | Latency    | Notes                      |
|------------------|------------|----------------------------|
| stop_requested() | ~1–5 ns    | Relaxed atomic load        |
| token copy       | ~10 ns     | Ref-count increment        |
| request_cancel() | O(subtree) | Wakes waiters, kicks tasks |

### Summary

Use cancellation to terminate long-running coroutines, enforce deadlines, or compose `with_timeout()`-style utilities.
For task-level cancellation prefer `JoinHandle::cancel()` / `TaskScope` — they use the same tree.

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