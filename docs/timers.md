# Timer API

Timers in **uvent** are managed by the internal **TimerWheel**, providing high-performance scheduling for both one-shot
and repeating events.  
Each timer is lightweight and coroutine-safe, integrating seamlessly into the event loop.

---

## TimerType

```cpp
enum TimerType {
    TIMEOUT,   // fires once
    INTERVAL   // fires repeatedly
};
```

---

## Timer class

```cpp
class alignas(32) Timer {
public:
    explicit Timer(timer_duration_t duration, TimerType type = TIMEOUT);

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    void addFunction(std::function<void(std::any&)> f, std::any arg);
    void addFunction(std::function<void(std::any&)> f, std::any& arg);

public:
    timeout_t expiryTime;
    timer_duration_t duration_ms;
    TimerType type;

private:
    std::coroutine_handle<> coro;
    bool active;
    uint64_t id;
    size_t slotIndex{0};
    size_t level{0};
};
```

### Notes

* `duration_ms` — timer period in milliseconds.
* `type` — `TIMEOUT` (one-shot) or `INTERVAL` (repeating).
* `addFunction` — binds a callback taking a `std::any&` parameter.
  This allows type-safe callback payloads without manual casts.
* Timers cannot be copied or moved.
* Internally scheduled into the thread-local `TimerWheel`.

---

## Scheduling

```cpp
/**
 * @brief Schedules a timer in timer wheel.
 *
 * Adds the given timer instance into timer wheel handler,
 * allowing it to be triggered after its configured expiry.
 *
 * @param timer Pointer to a valid timer object.
 *
 * @note If the timer type is set to TIMEOUT, it will fire once;
 *       otherwise, it will repeat indefinitely.
 *
 * @warning This method does not check whether the timer is initialized
 *          or already active. Use only with properly constructed and inactive timers.
 */
inline void spawn_timer(utils::Timer* timer);
```

---

## Timeout coroutine

```cpp
void addFunction(std::function<void(std::any&)> f, std::any arg);

void addFunction(std::function<void(std::any&)> f, std::any& arg);
```

Executes a coroutine that invokes the provided callback after the specified timeout expires.

---

## Examples

### TIMEOUT (one-shot)

A `TIMEOUT` timer fires **once** after its duration and is then automatically destroyed.
Use it for delayed actions such as timeouts, retries, or deferred tasks.

```cpp
task::Awaitable<void> demo() {
    auto* t = new utils::Timer(2000, TimerType::TIMEOUT);

    std::string payload = "hello timer";

    t->addFunction([](std::any& value) {
        auto& str = std::any_cast<std::string&>(value);
        std::cout << "Timer fired: " << str << std::endl;
    }, payload);

    spawn_timer(t);
    co_return;
}
```

### INTERVAL (repeating)

An `INTERVAL` timer fires repeatedly at a fixed rate until stopped or until the runtime shuts down.
Keep the payload object alive for as long as the timer is active.

```cpp
task::Awaitable<void> demo() {
    auto* t = new utils::Timer(1000, TimerType::INTERVAL);

    auto payload = std::make_shared<std::string>("tick");

    t->addFunction([](std::any& value) {
        auto& msg = std::any_cast<std::shared_ptr<std::string>&>(value);
        std::cout << "Timer fired: " << *msg << std::endl;
    }, payload);

    spawn_timer(t);
    co_return;
}
```

---

## Typical mistakes

!!! warning "Uninitialized timers"
Always construct a `Timer` with a valid duration before calling `spawn_timer()`.

!!! warning "Active timers"
Do not re-use a timer that is already active inside the TimerWheel.

!!! warning "Callback misuse"
Ensure that the `std::any` argument passed to `addFunction()` remains valid until the timer fires.

!!! warning "Lifetime"
Do **not** manually delete the `Timer` object — it is automatically cleaned up by the runtime when it expires or is
cancelled.

---

### Summary

| Feature            | Description                                   |
|--------------------|-----------------------------------------------|
| `TIMEOUT`          | Fires once, then destroyed automatically      |
| `INTERVAL`         | Fires repeatedly until runtime shutdown       |
| `std::any` payload | Allows passing arbitrary typed data safely    |
| `spawn_timer()`    | Inserts timer into the internal wheel         |
| Thread-safe        | Works inside per-thread `TimerWheel` contexts |