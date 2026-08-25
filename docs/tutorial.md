# Tutorial

A step-by-step walkthrough of everything uvent offers: the runtime, coroutines,
TCP servers and clients, timeouts, DNS resolution, Happy Eyeballs, timers,
synchronization primitives, channels, structured concurrency with
cancellation, and `select`. Each step builds on the previous one and ends
with a complete, runnable program.

Reference pages go deeper on every topic – this page is the guided path
through them.

---

## Step 0 – Add uvent to your project

The recommended way is CMake FetchContent:

```cmake
include(FetchContent)

FetchContent_Declare(uvent
        GIT_REPOSITORY https://github.com/Usub-Foundation/uvent.git
        GIT_TAG main
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(uvent)

target_link_libraries(${PROJECT_NAME} PRIVATE usub::uvent)
```

uvent picks the I/O backend for your platform automatically: `epoll` on Linux
(or `io_uring` with `-DUVENT_ENABLE_IO_URING=ON`, kernel 5.6+), `kqueue` on
macOS/BSD, IOCP on Windows. Your application code is identical on all of them.

There is one more build-level switch worth knowing about early:

* **Default (shared runtime).** All worker threads share one poller and one
  global task queue; coroutines and sockets are thread-agnostic.
* **`-DUVENT_ENABLE_REUSEADDR=ON` (sharded runtime).** Every worker thread owns
  its poller and timer wheel, listeners shard the port via `SO_REUSEPORT`, and
  a socket must stay on the thread that owns it. This trades flexibility for
  hot-path speed.

Everything in this tutorial works in both modes.

---

## Step 1 – Start the runtime and run your first coroutine

The runtime is the `usub::Uvent` object: it owns the worker threads and drives
the event loop. Work is expressed as coroutines returning
`task::Awaitable<T>`, and scheduled with `system::co_spawn`.

```cpp
#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include <chrono>
#include <iostream>

using namespace usub::uvent;
using namespace std::chrono_literals;

task::Awaitable<void> hello()
{
    std::cout << "hello from a coroutine\n";
    co_await system::this_coroutine::sleep_for(500ms);
    std::cout << "resumed 500ms later, without blocking any thread\n";
    co_return;
}

int main()
{
    usub::Uvent uvent(4);          // 4 worker threads
    system::co_spawn(hello());     // enqueue into the global task queue
    uvent.run();                   // blocks until stop()
}
```

Key points:

* `co_spawn(coro())` puts the coroutine into the **global** queue – any worker
  picks it up.
* `system::this_coroutine::sleep_for(d)` parks the coroutine on the internal
  timer wheel. The thread stays free to run other work; the coroutine resumes
  when the timer fires. It returns `bool`: `false` means the sleep was cut
  short because the surrounding task was cancelled (Step 9).
* `uvent.run()` blocks the calling thread; `uvent.stop()` (from any thread or
  coroutine) requests a graceful shutdown.

If you need a coroutine to start on a *specific* worker thread, schedule it
before the loop starts:

```cpp
usub::Uvent uvent(4);
uvent.for_each_thread([](int threadIndex, thread::ThreadLocalStorage*) {
    system::co_spawn_static(perThreadInit(), threadIndex);
});
uvent.run();
```

See [Uvent](uvent.md) and [System Primitives](system_primitives.md).

---

## Step 2 – A TCP server

A listener is a `net::TCPServerSocket` (bind + listen happen in the
constructor). The preferred accept API takes a **handler coroutine**: it drains
the whole kernel backlog on every wakeup and spawns your handler once per
connection.

```cpp
#include <uvent/net/Socket.h>

task::Awaitable<void> clientCoro(net::TCPClientSocket socket)
{
    constexpr size_t max_read = 64 * 1024;
    utils::DynamicBuffer buffer;
    buffer.reserve(max_read);

    socket.set_timeout_ms(5000);   // idle timeout for this connection

    for (;;)
    {
        buffer.clear();
        ssize_t n = co_await socket.async_read(buffer, max_read);
        if (n <= 0)                 // peer closed, error, or timeout
        {
            socket.shutdown();
            break;
        }
        co_await socket.async_write(const_cast<uint8_t*>(buffer.data()), buffer.size());
    }
    co_return;
}

task::Awaitable<void> listenerCoro()
{
    auto acceptor = new net::TCPServerSocket{"0.0.0.0", 45900};
    for (;;)
        co_await acceptor->async_accept(clientCoro);   // one handler per client
}

int main()
{
    settings::timeout_duration_ms = 5000;  // default idle timeout for new sockets
    usub::Uvent uvent(4);
    system::co_spawn(listenerCoro());
    uvent.run();
}
```

Key points:

* `async_read(DynamicBuffer&, max)` resumes once with whatever arrived
  (`> 0`), `0` on EOF, negative on error.
* `async_write(ptr, size)` writes the whole buffer, suspending on
  back-pressure.
* `set_timeout_ms` arms the per-socket inactivity timer; an idle connection
  wakes its parked read with a timeout instead of leaking forever.
* Binding an IPv6 listener: pass the family explicitly –
  `net::TCPServerSocket{"::1", 45900, SOMAXCONN, utils::net::IPV6}`.

The legacy one-at-a-time accept (`auto s = co_await acceptor.async_accept();`)
still exists but is deprecated – in edge-triggered backends it can silently
skip connections that arrived in the same batch. Details in
[Socket](socket.md).

---

## Step 3 – A TCP client

`async_connect(host, port[, timeout])` creates the socket, resolves the name
(asynchronously – see the next step), performs a non-blocking connect and
returns `std::nullopt` on success or a `ConnectError`:

```cpp
task::Awaitable<void> clientDemo()
{
    net::TCPClientSocket sock;
    auto err = co_await sock.async_connect("127.0.0.1", "45900", 2000ms);
    if (err)
    {
        // ConnectError::{GetAddrInfoFailed, SocketCreationFailed,
        //                ConnectFailed, Timeout, Unknown}
        co_return;
    }

    uint8_t ping[] = "ping";
    co_await sock.async_write(ping, 4);

    utils::DynamicBuffer buf;
    buf.reserve(64);
    ssize_t n = co_await sock.async_read(buf, 64);
    if (n > 0) { /* use buf.data(), n bytes */ }

    sock.shutdown();
    co_return;
}
```

To connect over IPv6, set the family on the socket first: `sock.ipv =
utils::net::IPV6;` – the resolver then asks for AAAA records only.

---

## Step 4 – Resolve names without blocking

`async_connect` already resolves for you, but the resolver is public API:

```cpp
#include <uvent/net/Resolver.h>

addrinfo hints{};
hints.ai_family   = AF_UNSPEC;
hints.ai_socktype = SOCK_STREAM;

auto r = co_await net::async_resolve("example.org", "443", hints);
if (!r) { /* gai error code: gai_strerror(r.error()) */ }
for (const addrinfo* ai = r->get(); ai; ai = ai->ai_next) { /* ... */ }
```

* IP literals (`"127.0.0.1"`, `"::1"`) resolve **inline** – no suspension at
  all (numeric fast path).
* Hostnames are processed by a lazy pool of `getaddrinfo` workers
  (`settings::resolver_threads`, default 2). The pool is created on the first
  real lookup; a process that never resolves names never spawns extra threads.
* The awaiting coroutine is resumed immediately when the lookup completes –
  parked pollers are signalled through the wake channel, so there is no hidden
  50 ms tick.

Details: [Name Resolution & Happy Eyeballs](resolver.md).

---

## Step 5 – Dual-stack connects with Happy Eyeballs

Connecting to the *first* resolved address of a dual-stack host is fragile: if
its IPv6 path is broken you wait out a full timeout before trying IPv4.
`net::connect_happy` implements RFC 8305 – it races the addresses and hands
you the first socket that actually connects:

```cpp
#include <uvent/net/HappyEyeballs.h>

task::Awaitable<void> fetch()
{
    auto res = co_await net::connect_happy("example.org", "443");
    if (!res)
        co_return;                    // res.error() is the last ConnectError

    net::TCPClientSocket sock = std::move(*res);
    // sock.ipv tells you which family won
}
```

What happens under the hood, in order:

1. AAAA and A queries run **in parallel**; the AAAA answer starts the race
   immediately, an A answer that lands first is held `resolution_delay`
   (50 ms) as a grace period, and a late answer joins the running race.
2. Addresses interleave by family, IPv6 first.
3. Attempts start `attempt_delay` (250 ms) apart; the first successful connect
   wins.
4. A *failed* attempt (instant RST) starts the next address immediately –
   broken-IPv6 hosts fall back to IPv4 in milliseconds.
5. Losers finish quietly in the background and close themselves.

Everything is tunable per call:

```cpp
net::HappyEyeballsOptions opts;
opts.attempt_delay    = std::chrono::milliseconds(100);
opts.attempt_timeout  = std::chrono::milliseconds(3000);
opts.max_attempts     = 6;
opts.resolution_delay = std::chrono::milliseconds(50);

auto res = co_await net::connect_happy("example.org", "443", opts);
```

If you already hold the addresses (your own service discovery, a config file),
skip resolution and race them directly, in your order:

```cpp
std::vector<net::ResolvedAddr> addrs;
addrs.push_back({"2001:db8::10", utils::net::IPV6});
addrs.push_back({"192.0.2.10",   utils::net::IPV4});
auto res = co_await net::connect_happy_addrs(std::move(addrs), "443", opts);
```

---

## Step 6 – Timers beyond sleep_for

`sleep_for` covers most needs, but the timer wheel is directly accessible when
you want a fire-and-forget callback instead of a suspended coroutine:

```cpp
#include <uvent/utils/timer/Timer.h>

auto* t = new utils::Timer(1000 /* ms */);
t->addFunction([](std::any& payload) {
    std::cout << "timer fired\n";
}, std::any{});
system::spawn_timer(t);
```

A timer fires exactly once; it can carry either a callback (`addFunction`) or
a coroutine (`addCoroutine` / `bind`). See [Timers](timers.md).

---

## Step 7 – Synchronize coroutines

`uvent::sync` ships coroutine-native primitives – waiters suspend instead of
blocking threads, and no allocations happen on the fast path:

| Primitive | Use case |
|---|---|
| `AsyncMutex` | exclusive access to shared state |
| `AsyncSemaphore` | bounded concurrency (N slots) |
| `AsyncEvent` | one-shot / resettable signal |
| `WaitGroup` | "wait until N tasks finish" |
| `AsyncBarrier` | phase synchronization of N coroutines |
| `CancellationSource` / `CancellationToken` | cooperative cancellation |

All blocking operations are cancellation-aware: when the awaiting task is
cancelled they return `false` / `std::nullopt` / an empty `Guard` instead of
the resource (details in Step 9 and [Cancellation](cancellation.md)).

The mutex in action:

```cpp
#include <uvent/sync/AsyncMutex.h>

static sync::AsyncMutex g_mutex;
static int g_counter = 0;

task::Awaitable<void> worker()
{
    auto guard = co_await g_mutex.lock();   // RAII guard, unlocks on scope exit
    ++g_counter;
    co_await system::this_coroutine::sleep_for(10ms); // lock held across suspension - fine
    co_return;
}
```

Full API and the other primitives: [Synchronization](synchronization.md).

---

## Step 8 – Message passing with channels

`AsyncChannel<Ts...>` is a Go-style buffered MPMC channel: `send` suspends when
the buffer is full (back-pressure), `recv` suspends when it is empty.

```cpp
#include <uvent/sync/AsyncChannel.h>

sync::AsyncChannel<int> jobs{1024};

task::Awaitable<void> producer()
{
    for (int i = 0; i < 100; ++i)
        co_await jobs.send(i);        // false => channel closed
    jobs.close();
    co_return;
}

task::Awaitable<void> consumer()
{
    for (;;)
    {
        auto msg = co_await jobs.recv();
        if (!msg)                     // closed AND drained
            co_return;
        auto& [value] = *msg;
        // process value
    }
}

// spawn one producer and N consumers with co_spawn(...)
```

To wait on several channels at once, `select_recv` (all channels must carry
the same value type):

```cpp
auto res = co_await select_recv(ch1, ch2);
if (!res) { /* every channel closed */ }
auto [channelIndex, tuple] = *res;
```

For heterogeneous waits – a channel *and* a timeout *and* a cancellation
token – use `sync::select` (Step 9 and [Select](channels/select.md)).

Non-blocking variants (`try_send`, `try_recv_into`) and the `operator<<` sugar
are covered in [Channels](channels/channels.md) and
[Select](channels/select.md).

### Unbounded variant

`AsyncUnboundedChannel<Ts...>` (`<uvent/sync/AsyncUnboundedChannel.h>`) has the
same receive-side API and works with `select_recv`, but is backed by a
lock-free segmented queue with no capacity limit:

- `send`/`try_send` never suspend and never fail because of a full buffer –
  the only failure is a closed channel. There is **no back-pressure**; watch
  `size_relaxed()` if producers can outrun consumers.
- Because sending never touches the scheduler, `try_send` is safe to call from
  plain threads outside the runtime (e.g. a logging thread feeding a coroutine
  consumer).
- Do not mix bounded and unbounded channels in one `select_recv` call.

```cpp
sync::AsyncUnboundedChannel<std::string> log_lines;   // no capacity argument

// any thread:
log_lines.try_send("hello");

// coroutine:
while (auto line = co_await log_lines.recv())
    write(std::get<0>(*line));
```

---

## Step 9 – Structured concurrency: tasks, cancellation, select

`co_spawn` is fire-and-forget. When you need ownership – wait for a result,
cancel a subtree on shutdown, bound the lifetime of workers to a scope – use
the task layer (`uvent/tasks/Task.h`, pulled in by `uvent/Uvent.h`).

```cpp
task::Awaitable<int> compute();

task::Awaitable<void> parent()
{
    auto h = task::spawn(compute());   // JoinHandle<int>, task linked under parent()
    const int v = co_await h;          // join: value or rethrown exception
    co_return;
}
```

Groups of workers belong in a `TaskScope` (a nursery). Cancelling the scope
cancels every task spawned through it, transitively; `join()` waits for the
whole subtree:

```cpp
task::Awaitable<void> service(sync::AsyncUnboundedChannel<Job>& q)
{
    task::TaskScope scope;
    for (int i = 0; i < 4; ++i)
        scope.spawn(worker(q), i % 2);

    co_await system::this_coroutine::sleep_for(run_duration);
    co_await scope.cancel_and_join();  // cancel everything, wait until it drained
}
```

A worker reacts to cancellation wherever it suspends – and can multiplex a
channel with a timeout via `sync::select`:

```cpp
task::Awaitable<void> worker(sync::AsyncUnboundedChannel<Job>& q)
{
    for (;;)
    {
        auto r = co_await sync::select(q.recv_op(), sync::sleep_op(1s));
        if (r.cancelled())
            co_return;                 // our scope was cancelled
        if (r.is<1>())
        {
            heartbeat();               // 1s with no job
            continue;
        }
        auto& job = r.get<0>();
        if (!job)
            co_return;                 // queue closed
        process(*job);
    }
}
```

What cancellation does to in-flight operations: `sleep_for` returns `false`,
socket reads/writes return `-1` with `errno == ECANCELED`, channel `recv`
returns `std::nullopt`, `mutex.lock()` returns an empty guard, `select`
returns `cancelled()`. Compute-only loops should poll
`system::this_coroutine::cancel_requested()`.

Two more tools from this layer:

* **Thread-affinity types.** A coroutine that owns a socket can be declared
  `task::LocalAwaitable<void>` – then `co_spawn` (the thread-migrating entry
  point) rejects it at compile time; only `co_spawn_static`/`spawn(aw, tid)`
  accept it. Channels likewise refuse thread-affine payloads
  (`uvent::is_thread_affine`).
* **Introspection.** Build with `-DUVENT_TASK_INTROSPECTION=ON` and call
  `introspection::dump()` to list every live coroutine with its wait reason,
  wait duration and trace id (`this_coroutine::set_trace_id` /
  `set_name`). See [Introspection](introspection.md).

Full reference: [Tasks](tasks.md), [Cancellation](cancellation.md),
[Select](channels/select.md). Runnable demo:
`examples/main_structured.cpp`.

---

## Step 10 – Tune the runtime

All knobs live in `usub::uvent::settings` and are plain globals – set them
before `uvent.run()`:

```cpp
settings::timeout_duration_ms = 20000; // default socket inactivity timeout
settings::resolver_threads    = 4;     // getaddrinfo pool (connect_happy uses 2 lookups/call)
settings::idle_fallback_ms    = 50;    // idle poll tick; cross-thread wakeups don't wait for it
settings::max_transfer_stack_depth = 512 * 1024; // bounce symmetric transfers via the scheduler past this depth
settings::coop_budget         = 128;   // fast-path completions per resume before a forced yield
```

`coop_budget` is the cooperative-scheduling guard: a coroutine that keeps
completing channel/socket operations without ever suspending is pushed back
through the run queue after this many completions, so neighbours on the same
worker keep running.

`max_transfer_stack_depth` exists because unoptimised builds (-O0/-O1,
sanitizers) do not tail-call coroutine hand-offs; without the guard a loop of
synchronously completing `co_await`s would grow the native stack until it
overflows. Workers measure their stack depth at each hand-off and, past the
threshold, enqueue the continuation instead of resuming it inline.

The full list (timer wheel depth, EINTR retry budgets, batch sizes) is in
[Settings](settings.md).

---

## Step 11 – Putting it all together

An echo server plus a client that finds it via Happy Eyeballs, coordinated
through a channel:

```cpp
#include <uvent/Uvent.h>
#include <uvent/net/Socket.h>
#include <uvent/net/HappyEyeballs.h>
#include <uvent/sync/AsyncChannel.h>

#include <cstdio>

using namespace usub::uvent;
using namespace std::chrono_literals;

static sync::AsyncChannel<int> done{1};

task::Awaitable<void> echoHandler(net::TCPClientSocket socket)
{
    utils::DynamicBuffer buf;
    buf.reserve(1024);
    socket.set_timeout_ms(5000);
    ssize_t n = co_await socket.async_read(buf, 1024);
    if (n > 0)
        co_await socket.async_write(const_cast<uint8_t*>(buf.data()), buf.size());
    socket.shutdown();
    co_return;
}

task::Awaitable<void> listenerCoro()
{
    auto acceptor = new net::TCPServerSocket{"127.0.0.1", 45900};
    for (;;)
        co_await acceptor->async_accept(echoHandler);
}

task::Awaitable<void> clientCoro()
{
    co_await system::this_coroutine::sleep_for(100ms);  // let the listener bind

    // "localhost" resolves to ::1 and 127.0.0.1; the race sorts out
    // which one actually accepts.
    auto res = co_await net::connect_happy("localhost", "45900");
    if (!res)
    {
        std::printf("connect failed\n");
        co_await done.send(1);
        co_return;
    }

    uint8_t ping[] = "ping";
    co_await res->async_write(ping, 4);

    utils::DynamicBuffer buf;
    buf.reserve(64);
    ssize_t n = co_await res->async_read(buf, 64);
    std::printf("echo roundtrip: %zd bytes, winner=%s\n", n,
                res->ipv == utils::net::IPV6 ? "v6" : "v4");
    res->shutdown();
    co_await done.send(0);
    co_return;
}

task::Awaitable<void> supervisor(usub::Uvent& uvent)
{
    (void)co_await done.recv();   // wait for the client to finish
    uvent.stop();                 // graceful shutdown
    co_return;
}

int main()
{
    usub::Uvent uvent(2);
    system::co_spawn(listenerCoro());
    system::co_spawn(clientCoro());
    system::co_spawn(supervisor(uvent));
    uvent.run();
    return 0;
}
```

---

## Where to go next

* [Socket](socket.md) – the full socket API surface, including UDP and the
  low-level awaiters.
* [Name Resolution & Happy Eyeballs](resolver.md) – resolver internals, race
  timings, the poller wake channel.
* [Awaitable](awaitable.md) / [Awaitable Frame](awaitable_frame.md) – how the
  coroutine machinery works and how to plug in custom frames.
* [Tasks](tasks.md) / [Cancellation](cancellation.md) /
  [Introspection](introspection.md) – the structured-concurrency layer in
  depth.
* `examples/` in the repository – runnable smokes for every subsystem
  (`main.cpp`, `main_happy_smoke.cpp`, `main_resolve_smoke.cpp`,
  `main_timers.cpp`, `main_channels_example.cpp`, `main_select_example.cpp`,
  `main_structured.cpp`).
