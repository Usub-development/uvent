# Tasks & Structured Concurrency

`co_spawn` is fire-and-forget: nothing owns the coroutine, nothing can cancel
it, and nothing can wait for it. The task layer adds ownership on top of the
same scheduler: `task::spawn` returns a `JoinHandle`, handles form a
cancellation tree, and `TaskScope` groups tasks so they can be cancelled and
awaited together.

Header: `uvent/tasks/Task.h` (included by `uvent/Uvent.h`).

---

## spawn

```cpp
template <class V, class F> JoinHandle<V> task::spawn(Awaitable<V, F> aw);
template <class V, class F> JoinHandle<V> task::spawn(Awaitable<V, F> aw, int tid);
template <class V, class F> JoinHandle<V> task::spawn_under(sync::CancelState* parent, int tid, Awaitable<V, F> aw);
```

* `spawn(aw)` pins the task to the **current worker thread** when called from
  inside the pool, otherwise pushes it to the shared queue (`tid == -1`).
* `spawn(aw, tid)` pins the task to worker `tid`.
* The new task is linked **under the current task's cancel state**: cancelling
  the parent cancels every task it spawned, transitively. `spawn` from outside
  any task creates a root.

One allocation per spawn (the shared `TaskState`). Plain `co_spawn` /
`co_spawn_static` stay allocation-free and detached – they also *clear* any
inherited cancel state, so legacy code keeps its exact semantics.

```cpp
task::Awaitable<int> compute();

task::Awaitable<void> parent()
{
    auto h = task::spawn(compute());
    const int v = co_await h;
    co_return;
}
```

---

## JoinHandle

```cpp
JoinHandle<V> h = task::spawn(coro());

co_await h;        // waits for completion, returns V (rethrows the task's exception)
h.done();          // completed?
h.cancel();        // request cancellation of this task and its subtree
h.token();         // CancellationToken observing this task
h.detach();        // drop ownership, task keeps running
```

* Move-only. Destroying a handle **detaches** (does not cancel) – like
  `tokio::JoinHandle`.
* `co_await h` is **not** interruptible by the joiner's own cancellation: it
  returns when the task completes. Since cancelling a parent also cancels its
  children, a cancelled tree drains and joins terminate.
* Awaiting the same handle twice after a non-void result is undefined (the
  value is moved out once).

---

## TaskScope

A nursery: tasks spawned through a scope belong to it.

```cpp
task::Awaitable<void> handler()
{
    task::TaskScope scope;                    // child of the current task
    scope.spawn(worker(0), 0);
    scope.spawn(worker(1), 1);
    auto h = scope.spawn(producer());

    co_await scope.join();                    // wait until the whole subtree is done
    // or:
    co_await scope.cancel_and_join();         // cancel everything, then wait
}
```

* `scope.join()` waits for **all** tasks in the subtree (children of children
  included), not just direct spawns.
* `~TaskScope()` requests cancellation of everything still running and
  detaches. It does **not** block: if the tasks reference stack state of the
  enclosing coroutine, `co_await scope.join()` (or `cancel_and_join()`)
  before leaving the scope – that is the structured-concurrency contract.
* `TaskScope(nullptr)` creates a detached scope (not linked to the current
  task); `TaskScope(token)` links under an explicit token.

---

## How cancellation reaches a task

`h.cancel()` (or cancelling any ancestor) marks the task's `CancelState` and
delivers a *kick* to the worker thread that owns the task. The kick runs on
the owner between coroutine resumes, walks to the task's innermost suspended
frame and invokes that awaiter's cancel hook:

* `sleep_for` – the timer is removed, the coroutine resumes immediately,
  `sleep_for` returns `false`;
* socket `async_read` / `async_write` / `async_accept` – the pending wait is
  aborted, the operation returns `-1` with `errno == ECANCELED`
  (accept returns `std::nullopt`);
* channel `recv`/`send`, `AsyncEvent::wait`, `AsyncSemaphore::acquire`,
  `WaitGroup::wait`, `AsyncMutex::lock`, `select` – the waiter is unlinked and
  the operation reports cancellation (`false` / `std::nullopt` / empty
  `Guard` / `SelectResult::cancelled()`).

A task that is *running* (not suspended in a cancellable awaiter) is never
interrupted preemptively: it observes cancellation at its next cancellable
suspension point, or explicitly via
`system::this_coroutine::cancel_requested()`.

```cpp
task::Awaitable<void> pump()
{
    while (!system::this_coroutine::cancel_requested())
    {
        if (!co_await system::this_coroutine::sleep_for(50ms))
            break;
        tick();
    }
}
```

> **Runtime modes.** Prompt kick delivery requires the sharded runtime
> (`UVENT_ENABLE_REUSEADDR`, the default), where every task has an owning
> worker. In the shared runtime coroutines migrate between workers, so kicks
> are disabled there: cancellation is lazy – it is observed at the next
> suspension point or wake-up instead of interrupting a wait in place.

---

## Thread affinity and spawn constraints

Some resources are pinned to the worker that owns them: sockets and timers in
the sharded runtime. Two compile-time guards keep them from migrating:

* `task::LocalAwaitable<T>` – a coroutine type that `co_spawn` (shared queue)
  **refuses at compile time**; it can only be started with `co_spawn_static`
  or `spawn(aw, tid)`. Use it for coroutines that own sockets or timers.

```cpp
task::LocalAwaitable<void> serve(net::TCPClientSocket s);   // pinned by type

system::co_spawn_static(serve(std::move(sock)), tid);       // ok
system::co_spawn(serve(std::move(sock)));                   // does not compile
```

* `uvent::is_thread_affine<T>` – trait marking types that must not cross
  threads (`net::Socket`, `utils::Timer` are pre-marked; specialize it for
  your own types). Channels `static_assert` that their payload is not
  thread-affine.

Socket I/O awaitables (`async_read`, …) are `LocalAwaitable` by construction.
