# Runtime Introspection

When a pod "hangs", logs rarely say which coroutine stopped moving. The
introspection layer keeps a registry of every live coroutine frame and can
dump, at any moment: what exists, what it is waiting on, for how long, and
with which trace id.

Header: `uvent/system/Introspection.h` (included by `uvent/Uvent.h`).

---

## Enabling

Tracking changes the coroutine frame layout and costs two short critical
sections per frame lifetime, so it is a build-time switch, **off by
default**:

```bash
cmake -DUVENT_TASK_INTROSPECTION=ON ...
```

The API exists in every build; with the option off `snapshot()` returns an
empty vector, `live_count()` returns 0 and `dump()` prints a notice, so call
sites need no `#ifdef`.

## API

```cpp
namespace usub::uvent::introspection
{
    struct TaskInfo
    {
        const void* frame;          // coroutine frame address
        const void* parent;         // awaiting (caller) frame, nullptr for roots
        const char* name;           // set via this_coroutine::set_name
        const char* wait_reason;    // "sleep", "channel.recv", "socket.read", ... or nullptr if running/ready
        uint64_t    trace_id;       // set via this_coroutine::set_trace_id, inherited by children
        int         thread_id;      // owning worker
        bool        cancel_requested;
        bool        has_cancel_hook; // parked in a cancellable wait right now
        uint64_t    age_ns;          // since frame creation
        uint64_t    waiting_ns;      // since the current wait was armed
    };

    std::vector<TaskInfo> snapshot();
    std::size_t live_count() noexcept;
    void dump(std::FILE* out = nullptr);   // nullptr -> stderr
}
```

`snapshot()` and `dump()` are safe to call from any thread at any time,
including from a signal-triggered watcher coroutine or an admin endpoint.

## Naming and tracing

```cpp
task::Awaitable<void> handle_request(Request rq)
{
    system::this_coroutine::set_name("handle_request");
    system::this_coroutine::set_trace_id(rq.id);
    ...
}
```

`trace_id` is inherited by every coroutine awaited or spawned under the
current task, so one `dump()` line correlates directly with the request logs.
Both setters and the per-frame propagation are plain stores; `set_name` /
`set_trace_id` accept no allocation and may be called on every request.

## Example output

```
uvent tasks: 4 live
  frame=0x7f2e6c001a30 thread=0 name=handle_request wait=socket.read trace=8412 age_ms=1204 waiting_ms=1198 cancellable-wait
  frame=0x7f2e6c003210 thread=1 name=worker wait=channel.recv trace=0 age_ms=95001 waiting_ms=13 cancellable-wait
  ...
```

A task with a large `waiting_ms` and `wait=socket.read` is your stuck
connection; `cancel-requested` without progress means a coroutine ignores its
token between suspension points.

## Typical wiring

```cpp
task::Awaitable<void> stall_watchdog()
{
    for (;;)
    {
        if (!co_await system::this_coroutine::sleep_for(30s))
            co_return;
        for (const auto& t : introspection::snapshot())
            if (t.waiting_ns > 60'000'000'000ull)
                std::fprintf(stderr, "stalled: frame=%p wait=%s trace=%llu\n", t.frame,
                             t.wait_reason ? t.wait_reason : "-",
                             static_cast<unsigned long long>(t.trace_id));
    }
}
```
