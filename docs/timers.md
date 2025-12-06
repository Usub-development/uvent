# Timer API

Timers in **uvent** provide lightweight, coroutine-compatible one-shot scheduling.  
Each timer fires exactly **once**, after the configured delay, and integrates directly into the event loop.

Timers are managed by the internal **TimerWheel**, which efficiently tracks large numbers of timeouts.

---

## Timer class

```cpp
class alignas(32) Timer
{
public:
    friend class core::EPoller;
    friend class TimerWheel;

    explicit Timer(timer_duration_t duration);

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    // Bind a callback executed when the timer expires
    void addFunction(std::function<void(std::any&)> f, std::any arg);
    void addFunction(std::function<void(std::any&)> f, std::any& arg);

    // Bind an Awaitable coroutine which will be resumed once
    template <class AwaitableT>
    void addCoroutine(AwaitableT&& aw)
    {
        using A = std::remove_reference_t<AwaitableT>;

        static_assert(
            requires(A a) { a.get_promise(); },
            "Timer::addCoroutine expects usub::uvent::task::Awaitable<>-like type"
        );

        auto* p = aw.get_promise();
        this->coro = p->get_coroutine_handle();
        this->active = true;
    }

    // Directly bind an existing coroutine handle
    void bind(std::coroutine_handle<> h) noexcept;

public:
    timeout_t      expiryTime;
    timer_duration_t duration_ms;

private:
    std::coroutine_handle<> coro;
    bool active;
    uint64_t id;
    size_t slotIndex{0};
    size_t level{0};
};
```

### Notes

* `duration_ms` — delay before the timer fires.
* `addFunction` — attach a callback that receives a `std::any&` payload.
* `addCoroutine` — attaches a uvent coroutine; it is resumed exactly once.
* Timers cannot be copied or moved.
* Timers are scheduled into the thread-local `TimerWheel`.

---

## Scheduling

```cpp
/**
 * @brief Schedules a timer in the timer wheel.
 *
 * Inserts the timer into the timer subsystem so it will fire once
 * after its configured duration.
 *
 * @param timer Pointer to a valid Timer instance.
 *
 * @warning This function does not validate whether the timer is already active.
 *          Only schedule freshly constructed timers.
 */
inline void spawn_timer(utils::Timer* timer);
```

---

## Timeout callbacks

```cpp
void addFunction(std::function<void(std::any&)> f, std::any arg);
void addFunction(std::function<void(std::any&)> f, std::any& arg);
```

The callback is executed **once** after the timer expires.
The stored `std::any` value is passed to the function at execution time.

---

## Examples

### One-shot timeout with a callback

```cpp
task::Awaitable<void> demo() {
    auto* t = new utils::Timer(2000); // 2 seconds

    std::string payload = "hello timer";

    t->addFunction([](std::any& value) {
        auto& s = std::any_cast<std::string&>(value);
        std::cout << "Timer fired: " << s << std::endl;
    }, payload);

    spawn_timer(t);
    co_return;
}
```

---

### One-shot resume of a coroutine

```cpp
task::Awaitable<void> delayed() {
    auto* t = new utils::Timer(1500);

    t->addCoroutine([]() -> task::Awaitable<void> {
        std::cout << "Timer resumed coroutine" << std::endl;
        co_return;
    }());

    spawn_timer(t);
    co_return;
}
```

---

## Typical mistakes

!!! warning "Uninitialized timers"
Always construct a `Timer` using a valid duration.

!!! warning "Active timers"
A timer must not be reused after scheduling; allocate a new one instead.

!!! warning "Callback misuse"
Ensure the `std::any` payload remains valid until firing (for example, avoid passing references to locals).

!!! warning "Lifetime"
Do **not** manually delete a `Timer`.
The runtime cleans it up after completion.

---

### Summary

| Feature           | Description                                  |
|-------------------|----------------------------------------------|
| One-shot fire     | Timer always triggers exactly once           |
| Callback support  | `addFunction(std::any&)`                     |
| Coroutine support | `addCoroutine(Awaitable)` and `bind(handle)` |
| `spawn_timer()`   | Schedules timer in thread-local wheel        |
| Lightweight       | Designed for high-throughput timeout usage   |