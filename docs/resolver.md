# Name Resolution & Happy Eyeballs

Asynchronous DNS resolution and RFC 8305 ("Happy Eyeballs v2") connection racing.

```cpp
#include "uvent/net/Resolver.h"       // async_resolve
#include "uvent/net/HappyEyeballs.h"  // connect_happy / connect_happy_addrs
```

---

## Asynchronous resolution

```cpp
task::Awaitable<std::expected<AddrInfoPtr, int>>
net::async_resolve(std::string host, std::string service, const addrinfo& hints);
```

Coroutine-friendly `getaddrinfo`. Returns an owning `AddrInfoPtr` (RAII wrapper,
calls `freeaddrinfo` on destruction) or the raw `gai` error code
(`std::unexpected`, printable via `gai_strerror`).

```cpp
addrinfo hints{};
hints.ai_family   = AF_UNSPEC;
hints.ai_socktype = SOCK_STREAM;

auto r = co_await net::async_resolve("example.org", "443", hints);
if (!r) { /* gai_strerror(r.error()) */ }
for (const addrinfo* ai = r->get(); ai; ai = ai->ai_next) { /* ... */ }
```

**How it works**

* **Numeric fast path.** Before suspending, the awaiter retries the query with
  `AI_NUMERICHOST`. IP literals (`"127.0.0.1"`, `"::1"`, `"fd00::2"`) resolve
  inline — no suspension, no worker pool, no extra latency. This is the common
  path for benchmarks and address-based `connect_happy_addrs` attempts.
* **Worker pool.** Non-numeric names are handed to a lazy singleton pool of
  blocking `getaddrinfo` workers (`settings::resolver_threads`, default `2`).
  The pool is created on the **first real lookup**, not at startup: a process
  that never resolves names never spawns these threads. Idle workers sleep on a
  condition variable.
* **Resumption.** The requesting coroutine is resumed on a uvent thread as soon
  as the lookup finishes; a parked poller is woken immediately via the
  [poller wake channel](#poller-wake), so resume latency does not depend on
  `settings::idle_fallback_ms`.

**Threading note.** `async_resolve` (and everything below) must be awaited from
a uvent thread.

---

## Happy Eyeballs (RFC 8305)

```cpp
struct HappyEyeballsOptions
{
    std::chrono::milliseconds attempt_delay{250};     // stagger between attempts (§5)
    std::chrono::milliseconds attempt_timeout{10000}; // per-attempt connect timeout
    size_t                    max_attempts{4};        // cap on raced addresses, 0 = unlimited
    std::chrono::milliseconds resolution_delay{50};   // AAAA grace period (§3), 0 = off
};

struct ResolvedAddr
{
    std::string     ip;                      // numeric literal
    utils::net::IPV ipv{utils::net::IPV4};
};

// Full flow: parallel AAAA/A resolution -> family interleave -> race.
task::Awaitable<std::expected<TCPClientSocket, ConnectError>>
net::connect_happy(std::string host, std::string port, HappyEyeballsOptions opts = {});

// Race over a caller-provided address list (caller's order, no resolution).
task::Awaitable<std::expected<TCPClientSocket, ConnectError>>
net::connect_happy_addrs(std::vector<ResolvedAddr> addrs, std::string port,
                         HappyEyeballsOptions opts = {});
```

Returns the **winning connected socket**, or the last `ConnectError` once every
attempt has been exhausted (`GetAddrInfoFailed` when resolution produced no
addresses at all).

```cpp
auto res = co_await net::connect_happy("example.org", "443");
if (res)
{
    co_await res->async_send(...);
}
```

### What it does

1. **Parallel resolution (§3).** `connect_happy` fires AAAA and A queries
   concurrently instead of a single `AF_UNSPEC` lookup:

    * the AAAA answer feeds its addresses in and starts the race immediately;
    * an A answer that lands **first** is held for `resolution_delay`
      (default 50 ms) to give AAAA a chance to win the start, then feeds in;
    * a **late** answer joins the race mid-flight: its addresses are woven into
      the unlaunched tail of the list, so a slow A record still provides an
      IPv4 fallback instead of being dropped.
    * IPv4-only hosts pay no penalty: the empty AAAA answer returns quickly and
      clears the hold.

2. **Family interleaving (§4).** Addresses alternate between families, IPv6
   first: `v6, v4, v6, v4, …`.

3. **Staggered attempts (§5).** The first address is tried immediately; each
   subsequent attempt starts `attempt_delay` after the previous one (one launch
   per stagger slot). The first successful `connect` wins the race.

4. **Early start on failure (§5).** A *failed* attempt (e.g. an instant RST)
   launches the next address immediately — it does not wait for its stagger
   slot. A fast-failing IPv6 path therefore falls back to IPv4 in
   milliseconds, not in `attempt_delay`.

5. **Losers are not cancelled.** Attempts that lose the race finish their
   `connect` on their own (bounded by `attempt_timeout`) and close themselves;
   a loser that manages to connect after the winner is shut down gracefully.
   The shared race state outlives the caller via `shared_ptr`, so returning
   early is safe.

### Typical timings

| Scenario                               | Time to a usable socket        |
|----------------------------------------|--------------------------------|
| First address alive                    | immediately                    |
| First address refuses (RST)            | ~0 ms (early start)            |
| First address blackholed (SYN dropped) | ~`attempt_delay` (stagger)     |
| All addresses dead                     | last attempt's failure/timeout |

### Notes

* Attempts connect by numeric IP and therefore always take the resolver's
  numeric fast path — the race never blocks on DNS.
* `max_attempts` caps the total number of raced addresses (after interleaving
  and late joins).
* In `UVENT_ENABLE_REUSEADDR` builds the whole race (attempts, pacer,
  resolution feeders) is pinned to the caller's thread; in shared-poller builds
  it is thread-agnostic.

### Testing

`examples/main_happy_smoke.cpp` (target `uvent_example_happy`) covers the
loopback scenarios. Real v6→v4 fallback with a blackholed SYN needs root and an
isolated network namespace:

```bash
sudo bash examples/run_happy_netns.sh   # reject / drop / v6live scenarios
```

---

## Poller wake

Cross-thread wakeups (resolver workers finishing a lookup, tasks pushed into
another thread's inbox) do not wait for a parked poller to time out. Each
poller owns a wake channel that any thread may signal:

| Backend  | Mechanism                                                                                                          |
|----------|--------------------------------------------------------------------------------------------------------------------|
| epoll    | `eventfd`, registered edge-triggered                                                                               |
| io_uring | `eventfd` + multishot poll armed by the ring owner (foreign threads only write the eventfd — `SINGLE_ISSUER` safe) |
| kqueue   | `EVFILT_USER` / `NOTE_TRIGGER`                                                                                     |
| IOCP     | `PostQueuedCompletionStatus` with a sentinel key                                                                   |

Signals are deduplicated with an atomic flag, so a burst of wakeups costs one
syscall. Without this channel a resume posted from a non-uvent thread could sit
until the poller's `settings::idle_fallback_ms` (50 ms) expired; with it, the
resume is near-instant.
