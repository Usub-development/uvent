# Timer API

Timers in **uvent** are scheduled and managed by the internal **TimerWheel** for efficient handling of timeouts and
intervals.

---

## TimerType

```cpp
enum TimerType {
    TIMEOUT,   // fires once
    INTERVAL   // fires repeatedly
};
```
## Timer class
```cpp
class alignas(32) Timer {
public:
    explicit Timer(timer_duration_t duration, TimerType type = TIMEOUT);

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    void addFunction(std::function<void(void*)>& function, void* functionValue);
    void addFunction(std::function<void(void*)>&& function, void* functionValue);

public:
    timeout_t expiryTime;
    timer_duration_t duration_ms;
    TimerType type;

private:
    // ...
};
```
### Notes
- `duration_ms` — timer period in milliseconds.
- `type` — whether it fires once (TIMEOUT) or repeatedly (INTERVAL).
- `addFunction` — binds a callback function (takes void* argument).
- Timers cannot be copied or moved.
- Internally scheduled into TimerWheel.

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

## Timeout coroutine
```cpp
task::Awaitable<void> timeout_coroutine(std::function<void(void*)> f, void* arg);
```
Runs a coroutine that executes the provided callback once the timer expires.

## Example

### Timeout
A `TIMEOUT` timer fires **once** after the specified duration and is then destroyed automatically.
It is suitable for scheduling single delayed actions (e.g., request timeouts, delayed tasks).
```cpp
void onTimeout(void* arg) {
    auto* message = static_cast<std::string*>(arg);
    fmt::print("Timer fired: {}\n", *message);
}

task::Awaitable<void> demo() {
    auto* t = new utils::Timer(2000, TimerType::TIMEOUT); 
    auto* payload = new std::string("hello timer");
    t->addFunction([](void* p){
        auto* s = static_cast<std::string*>(p);
        std::cout << "Timer fired: " << *s << std::endl;
        delete s; 
    }, payload);
    spawn_timer(t);

    co_return;
}
```
### Intervals
An `INTERVAL` timer fires repeatedly at the given period until it is explicitly stopped or the runtime shuts down.

```cpp
void onTimeout(void* arg) {
    auto* message = static_cast<std::string*>(arg);
    fmt::print("Timer fired: {}\n", *message);
}

task::Awaitable<void> demo() {
    auto* t = new utils::Timer(1000, TimerType::INTERVAL);

    // keep payload alive for the whole lifetime of the timer → no UAF
    auto* payload = new std::string("hello timer");

    t->addFunction([](void* p) {
        auto* s = static_cast<std::string*>(p);
        std::cout << "Timer fired: " << *s << std::endl;
    }, payload);

    spawn_timer(t);

    co_return;
}
```

## Typical mistakes

!!! warning "Uninitialized timers"
Always construct Timer with valid duration before calling spawn_timer.

!!! warning "Active timers"
Do not re-use a timer object while it is still active.

!!! warning "Callback misuse"
Ensure functionValue points to valid memory when the callback runs.

!!! warning "Use after free"
Don't try to free timer object. It will be destroyed automatically.