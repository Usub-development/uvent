# Socket API

Templated socket wrapper with coroutine-friendly I/O and explicit role/proto typing.

```cpp
template <Proto P, Role R>
class Socket;
using TCPServerSocket = Socket<Proto::TCP, Role::PASSIVE>;
using TCPClientSocket = Socket<Proto::TCP, Role::ACTIVE>;
using UDPBoundSocket  = Socket<Proto::UDP, Role::ACTIVE>;
using UDPSocket       = Socket<Proto::UDP, Role::PASSIVE>;
```

## Header layout (internal)

`SocketHeader` stores FD, timer id, state flags, and two coroutine slots (for reading and for writing operations).
State flags (bitmask): `CLOSED_MASK`, `BUSY_MASK`, `READING_MASK`, `WRITING_MASK`.

---

## Constructors & Assignment

```cpp
Socket() noexcept;
explicit Socket(SocketHeader* header) noexcept;          // wrap existing header (no FD dup)
static Socket from_existing(SocketHeader* header);       // initializes refcount state

// TCP PASSIVE only (bind+listen)
explicit Socket(std::string&  ip, int port=8080, int backlog=50,
                utils::net::IPV ipv=utils::net::IPV4,
                utils::net::SocketAddressType sat=utils::net::TCP) noexcept requires(P==Proto::TCP && R==Role::PASSIVE);

explicit Socket(std::string&& ip, int port=8080, int backlog=50,
                utils::net::IPV ipv=utils::net::IPV4,
                utils::net::SocketAddressType sat=utils::net::TCP) noexcept requires(P==Proto::TCP && R==Role::PASSIVE);

// copy/move
Socket(const Socket&) noexcept;
Socket(Socket&&) noexcept;
Socket& operator=(const Socket&) noexcept;
Socket& operator=(Socket&&) noexcept;

~Socket();
```

**Notes**

* TCP passive ctor: creates non-blocking FD, registers READ with poller.
* Copy increases internal refcount; move steals header.

---

## Introspection

```cpp
SocketHeader* get_raw_header();
```

---

## Accept (TCP server)

```cpp
task::Awaitable<std::optional<TCPClientSocket>,
  uvent::detail::AwaitableIOFrame<std::optional<TCPClientSocket>>>
async_accept() requires(P==Proto::TCP && R==Role::PASSIVE);
```

Returns a ready-to-use `TCPClientSocket` (non-blocking; READ registered) or `std::nullopt` on failure.

---

## Async I/O

```cpp
// READ: TCP ACTIVE or any UDP
task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>>
async_read(utils::DynamicBuffer& buf, size_t max_read_size)
requires((P==Proto::TCP && R==Role::ACTIVE) || (P==Proto::UDP));

// WRITE: TCP ACTIVE or any UDP
task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>>
async_write(uint8_t* data, size_t size)
requires((P==Proto::TCP && R==Role::ACTIVE) || (P==Proto::UDP));

// sendfile: TCP ACTIVE or any UDP
task::Awaitable<ssize_t, uvent::detail::AwaitableIOFrame<ssize_t>>
async_sendfile(int in_fd, off_t* offset, size_t count)
requires((P==Proto::TCP && R==Role::ACTIVE) || (P==Proto::UDP));
```

**Behavior**

* `async_read` waits for EPOLLIN and pulls chunks into `DynamicBuffer` until `max_read_size` or would-block.
  * Returns `>0` bytes read, `0` on EOF, `-1` on error, `-2` if `max_read_size` hit.
* `async_write` waits for EPOLLOUT and sends until would-block or done. Returns bytes written or `-1` on error.
* `async_sendfile` waits for EPOLLOUT, then calls `sendfile`. Returns bytes sent or `-1` on error.

---

## Sync I/O

```cpp
ssize_t read(utils::DynamicBuffer& buf, size_t max_read_size)
requires((P==Proto::TCP && R==Role::ACTIVE) || (P==Proto::UDP));

ssize_t write(uint8_t* data, size_t size)
requires((P==Proto::TCP && R==Role::ACTIVE) || (P==Proto::UDP));

ssize_t sendfile(int in_fd, off_t* offset, size_t count)
requires((P==Proto::TCP && R==Role::ACTIVE) || (P==Proto::UDP));
```

Return conventions mirror the async versions (`>0`, `0`, `-1`, `-2` for `read`).

---

## Connect (TCP client)

```cpp
task::Awaitable<std::optional<usub::utils::errors::ConnectError>,
  uvent::detail::AwaitableIOFrame<std::optional<usub::utils::errors::ConnectError>>>
async_connect(std::string&  host, std::string&  port)
requires(P==Proto::TCP && R==Role::ACTIVE);

task::Awaitable<std::optional<usub::utils::errors::ConnectError>,
  uvent::detail::AwaitableIOFrame<std::optional<usub::utils::errors::ConnectError>>>
async_connect(std::string&& host, std::string&& port)
requires(P==Proto::TCP && R==Role::ACTIVE);
```

Resolves address, creates non-blocking socket, initiates `connect`, waits for EPOLLOUT, checks error.

* Returns `std::nullopt` on success, or a specific `ConnectError` (`GetAddrInfoFailed`, `SocketCreationFailed`,
  `ConnectFailed` or `Unknown`).

### **Connect timeout support**

`async_connect()` supports an optional timeout parameter:

```cpp
async_connect(host, port, std::chrono::milliseconds timeout);
```

* If the timeout expires before the socket becomes writable, the coroutine completes with:

```cpp
ConnectError::Timeout
```

* Works uniformly on all platforms (`epoll`, `kqueue`, `IOCP`, `io_uring`).
* Timeout is implemented via the socket timer system (`set_timeout_ms` + timer wheel).

Example:

```cpp
auto ec = co_await sock.async_connect("1.2.3.4", "443",
    std::chrono::seconds{3});

if (ec && *ec == ConnectError::Timeout) {
    // handle timeout
}
```

---

## High-level send/receive helpers

```cpp
// Request/response pattern with chunked send + bounded receive.
task::Awaitable<std::expected<std::string, usub::utils::errors::SendError>,
  uvent::detail::AwaitableIOFrame<std::expected<std::string, usub::utils::errors::SendError>>>
async_send(uint8_t* data, size_t size, size_t chunkSize=16384, size_t maxSize=65536)
requires((P==Proto::TCP && R==Role::ACTIVE) || (P==Proto::UDP));

std::expected<std::string, usub::utils::errors::SendError>
send(uint8_t* data, size_t size, size_t chunkSize=16384, size_t maxSize=65536)
requires((P==Proto::TCP && R==Role::ACTIVE) || (P==Proto::UDP));
```

* On success returns response payload (up to `maxSize`).
* On failure returns `SendError` (`InvalidSocketFd`).

---

## Timers & Lifecycle

```cpp
void update_timeout(timer_duration_t new_duration) const; // refresh timer wheel entry
void shutdown();                                          // ::shutdown(fd, SHUT_RDWR)
void set_timeout_ms(timeout_t timeout = settings::timeout_duration_ms) const
  requires(P == Proto::TCP && R == Role::ACTIVE);         // Sets timeout to associated socket.
```

* update_timeout — refreshes the timer wheel entry with a new duration.
* shutdown — closes both directions with SHUT_RDWR.
* set_timeout_ms — overrides the timeout for this TCP client socket.
  * Default is `settings::timeout_duration_ms` (default 20000 milliseconds).
  * Must be called after socket initialization.

Destruction path (internal):

* `destroy()` → mark `CLOSED_MASK`, unregister from poller, retire header via QSBR.
* `remove()`  → unregister and mark closed (without QSBR retire).

---

## Thread-safety & State

* The header’s `state` uses atomic bitmasks:

  * `try_mark_busy()/clear_busy()`
  * `try_mark_reading()/clear_reading()`
  * `try_mark_writing()/clear_writing()`
  * `close_for_new_refs()` prevents new references before retirement.
* Designed for a multi-threaded event loop; avoid concurrent conflicting ops on the same socket unless you use the
  provided state guards.

---

## Client addr

To get client addr you can simply use:

```cpp
        /**
         * \brief Returns the client network address (IPv4 or IPv6) associated with this socket.
         *
         * The type alias \c client_addr_t is defined as:
         * \code
         * typedef std::variant<sockaddr_in, sockaddr_in6> client_addr_t;
         * \endcode
         * allowing the caller to handle both IPv4 and IPv6 endpoints transparently.
         *
         * \return The client address variant.
         */
        client_addr_t get_client_addr() const;

        /**
         * \brief Returns the client network address (IPv4 or IPv6) associated with this socket.
         *
         * Non-const overload allowing modifications to the returned structure if necessary.
         *
         * \return The client address variant.
         */
        client_addr_t get_client_addr();
```

both of them returning: `typedef std::variant<sockaddr_in, sockaddr_in6> client_addr_t;`.

To get client's IP version you can simply use:

```cpp
        /**
          * \brief Returns the IP version (IPv4 or IPv6) of the connected peer.
          *
          * Determines whether the underlying active TCP socket is using an IPv4 or IPv6 address family.
          *
          * \return utils::net::IPV enum value indicating the IP version.
          */
        [[nodiscard]] utils::net::IPV get_client_ipv() const requires (p == Proto::TCP && r ==
            Role::ACTIVE);
```

Returns:

```cpp
    enum IPV {
        IPV4 = 0x0,
        IPV6 = 0x1,
    };
```

It reflects the actual IP version of the connected client.

---

## Return/Error Summary

* `ssize_t` I/O: `>0` bytes, `0` EOF (read), `-1` error, `-2` read cap reached.
* `std::optional<T>`: `std::nullopt` = success/absent; set = error/value present.
* `std::expected<T,E>`: value on success, error enum on failure.