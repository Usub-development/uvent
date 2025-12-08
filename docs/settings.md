# uvent Settings

This document describes all configurable runtime settings available in **uvent**.
All settings are declared in `usub::uvent::settings` and can be modified at application startup.
Default values come from the current implementation.

---

## Timer Wheel

### `tw_levels`

**Type:** `int`
**Default:** `4`

Defines the number of hierarchical levels in the timer wheel.
Each level expands the timer resolution exponentially.

Maximum representable timeout:

```
max_timeout = 256^(tw_levels - 1)
```

With the default `tw_levels = 4`, the maximum range becomes:

```
256^(4 - 1) = 256^3 = 16,777,216 ticks
```

---

## Connection Handling

### `timeout_duration_ms`

**Type:** `uint64_t`
**Default:** `20000` ms (20 seconds)

Specifies the maximum allowed inactivity time for a client connection.
When this time elapses without activity, the connection is closed.

---

## EINTR Retry Behavior

### `max_read_retries`

**Type:** `int`
**Default:** `100`

Maximum number of consecutive `EINTR` interruptions allowed during a read operation before it is considered failed.

### `max_write_retries`

**Type:** `int`
**Default:** `100`

Maximum number of consecutive `EINTR` interruptions tolerated during a write operation.

---

## Timer Wheel Internal Buffers

### `max_pre_allocated_timer_wheel_operations_items`

**Type:** `int`
**Default:** `256`

Defines how many timer operations (add/update/delete) can be batched and processed per iteration.

---

## Task Scheduling Buffers

### `max_pre_allocated_tasks_items`

**Type:** `int`
**Default:** `1024`

Maximum number of queued tasks pulled and executed in a single batch.

---

## Socket Cleanup Buffers

### `max_pre_allocated_tmp_sockets_items`

**Type:** `int`
**Default:** `1024`

Specifies how many sockets are batched together during deferred cleanup cycles.

---

## Coroutine Cleanup Buffers

### `max_pre_allocated_tmp_coroutines_items`

**Type:** `int`
**Default:** `256`

Defines the maximum number of completed coroutine handles destroyed in one cleanup pass.

---

## Worker Thread Idle Behavior

### `idle_fallback_ms`

**Type:** `int`
**Default:** `50` ms

Idle worker threads wake up at this interval to check for new tasks when their local queues are empty.