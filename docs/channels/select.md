# Select

`sync::select` waits on several heterogeneous operations at once and resumes
when the first one completes — Go's `select` for uvent coroutines. Branches
are *ops*: lightweight descriptors obtained from channels, tokens, events,
semaphores, wait groups, or timers.

Header: `uvent/sync/Select.h` (included by `uvent/Uvent.h`).

---

# Overview

```cpp
sync::AsyncUnboundedChannel<Job> jobs;
sync::CancellationToken tok = ...;

auto r = co_await sync::select(jobs.recv_op(),
                               tok.on_cancel_op(),
                               sync::sleep_op(std::chrono::seconds(5)));

if (r.cancelled())            // the *current task* was cancelled
    co_return;
switch (r.index)
{
case 0: use(*r.get<0>()); break;   // job received (std::optional<Job> — nullopt = channel closed)
case 1: co_return;                 // token fired
case 2: on_idle_timeout(); break;  // 5s passed
}
```

`select` registers one waiter per branch, parks the coroutine once, and the
first branch to fire wins a CAS on a shared word; losers never resume the
coroutine twice. Between rounds each branch is polled directly, so an
already-ready branch completes without suspending at all.

# SelectResult

```cpp
template <class... Ops> struct SelectResult
{
    int32_t index;                 // winning branch, -1 when cancelled
    std::variant<std::monostate, typename Ops::result_type...> value;

    bool cancelled() const;        // own task cancelled while selecting
    template <std::size_t I> auto& get();     // result of branch I
    template <std::size_t I> bool is() const; // index == I
};
```

Branch result types:

| Op                         | `result_type`                                                |
|----------------------------|--------------------------------------------------------------|
| `channel.recv_op()`        | `std::optional<value_type>` (`nullopt` = closed and drained) |
| `channel.send_op(v)`       | `bool` (`false` = closed)                                    |
| `token.on_cancel_op()`     | `std::monostate`                                             |
| `event.wait_op()`          | `std::monostate`                                             |
| `sem.acquire_op()`         | `std::monostate` (the permit is held on completion)          |
| `wg.wait_op()`             | `std::monostate`                                             |
| `sync::sleep_op(duration)` | `std::monostate`                                             |

# Fairness

Polling starts from a per-thread rotating index, so two永 always-ready
branches are taken alternately rather than the first one starving the rest.

# Cancellation

If the task executing `select` is cancelled, the wait is abandoned and the
result has `index == -1` (`cancelled() == true`). A *token branch*
(`on_cancel_op`) is the way to react to some **other** token; the implicit
path only reacts to the current task's own cancellation.

# Semantics notes

* Ops are level-triggered: a wake is a hint, the branch is re-polled before
  being reported, and the coroutine re-parks if the state was consumed by a
  competing consumer. Exactly one branch is reported per `select`.
* `sleep_op` arms its timer on the first park and keeps the original
  deadline across re-parks inside the same `select` call.
* A `select` in a loop creates fresh ops each iteration — a `sleep_op`
  restarts its timeout per iteration by design.
* All ops borrow their source (channel, token, …); the source must outlive
  the `select` call.

# select_recv (homogeneous shortcut)

The older API is still available for the common "N same-typed channels" case
and now rides on the same waiter machinery (no global wake event, no
thundering herd):

```cpp
task::Awaitable<void> consume(AsyncUnboundedChannel<Msg>& a, AsyncUnboundedChannel<Msg>& b)
{
    for (;;)
    {
        auto r = co_await sync::select_recv(a, b);   // optional<pair<index, value>>
        if (!r)
            break;                                    // every channel closed and drained
        handle(r->first, std::move(r->second));
    }
}
```

* Returns `std::nullopt` when **all** channels are closed and drained, or
  when the current task is cancelled.
* Channels that become closed-and-drained are dropped from the wait set;
  remaining channels keep being served.

# Example: worker with idle tick and shutdown

```cpp
task::Awaitable<void> worker(sync::AsyncUnboundedChannel<Job>& q)
{
    for (;;)
    {
        auto r = co_await sync::select(q.recv_op(), sync::sleep_op(1s));
        if (r.cancelled())
            co_return;                       // scope.cancel_and_join() reached us
        if (r.is<1>())
        {
            heartbeat();
            continue;
        }
        auto& job = r.get<0>();
        if (!job)
            co_return;                       // queue closed
        process(*job);
    }
}
```
