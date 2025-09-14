# Awaitable Frame

Coroutines in **uvent** are built on top of a small set of frame types that act as the *promise object* for each coroutine. They store state, results, and connect coroutines together inside the event loop.

---

## AwaitableFrameBase

`AwaitableFrameBase` is the common base for all coroutine frames.

It is responsible for:

- Storing the coroutine handle (`coro_`) and links to previous/next coroutines in a chain.
- Managing exceptions (`exception_`) thrown inside the coroutine.
- Tracking whether the coroutine is currently awaited (`is_awaited`, `set_awaited`, `unset_awaited`).
- Resuming execution (`resume()`) and scheduling destruction (`push_frame_to_be_destroyed`).
- Linking calling and next coroutines (`set_calling_coroutine`, `set_next_coroutine`).

In short: this base type is the glue that lets coroutines suspend/resume properly in the runtime.

---

## AwaitableFrame\<T\>

`AwaitableFrame<T>` extends the base with storage for a **return value of type T**.  
It provides:

- `return_value(T value)` — store the final result.
- `yield_value(T value)` — yield intermediate values.
- `get()` — extract the result (or rethrow exception).
- `get_return_object()` — builds an `Awaitable<T>` handle bound to this frame.

Lifecycle hooks:

- `initial_suspend()` — coroutine always suspends at start.
- `final_suspend()` — suspends at the end, ensures awaiting coroutine is resumed and the frame is cleaned up.
- `unhandled_exception()` — stores exception into the frame.

This is the standard frame type used for user-level coroutines returning a value.

---

## AwaitableFrame\<void\>

Specialization for coroutines that return `void`.  
Same as `AwaitableFrame<T>`, but with `return_void()` instead of `return_value()`.

---

## Why do we need frames?

C++ coroutines require a **promise type** that manages the coroutine’s lifecycle and results.  
In uvent:

- `AwaitableFrameBase` defines the common mechanics.
- `AwaitableFrame<T>` and `AwaitableFrame<void>` implement concrete promise types.
- `task::Awaitable` is the handle that user code sees, which internally wraps the frame.

This separation allows:

- Consistent scheduling (every coroutine frame can push itself into the task queue).
- Reference-counted lifetime management.
- Exception-safe propagation.
- Interoperability: frames can be resumed, chained, and destroyed by the runtime without leaking resources.

---

## Custom frames

## Custom Frames

You can plug your own promise/frame type into `task::Awaitable` — just **inherit from `AwaitableFrameBase`** and keep the minimal contract. This lets you tailor scheduling (e.g., start immediately vs. park first), result storage, or cleanup policy.

### Minimal contract
Your frame must:
- derive from `AwaitableFrameBase`;
- define `initial_suspend()` and `final_suspend()` (decide when to run and how to resume caller);
- implement `get_return_object()` that returns `task::Awaitable<T, YourFrame>` and stores `coro_`;
- provide `return_value(T)` / `return_void()`;
- provide `get()` (and rethrow `exception_` if set);
- implement `unhandled_exception()` (store to `exception_`);
- at the end of `final_suspend()` call `push_frame_to_be_destroyed()` and **unset** the caller’s `awaited` flag (via `prev_`), optionally re-enqueue the caller using `push_frame_into_task_queue`.

### Skeleton (value-returning)
```cpp
struct MyIntFrame : usub::uvent::detail::AwaitableFrameBase {
    bool has_ = false;
    alignas(int) unsigned char storage_[sizeof(int)]{};

    // start parked (change to suspend_never for "fire-and-run")
    std::suspend_always initial_suspend() noexcept { return {}; }

    // resume caller, then schedule destruction
    std::suspend_always final_suspend() noexcept {
        if (prev_) {
            auto caller = std::coroutine_handle<AwaitableFrameBase>::from_address(prev_.address());
            caller.promise().unset_awaited();
            // Optionally requeue caller on current thread’s task queue:
            // push_frame_into_task_queue(static_cast<std::coroutine_handle<>>(caller));
            prev_ = nullptr;
        }
        push_frame_to_be_destroyed();
        return {};
    }

    void unhandled_exception() { exception_ = std::current_exception(); }

    void return_value(int v) {
        new (&storage_) int(std::move(v));
        has_ = true;
    }

    int get() {
        if (exception_) std::rethrow_exception(exception_);
        return std::move(*std::launder(reinterpret_cast<int*>(&storage_)));
    }

    auto get_return_object() {
        coro_ = std::coroutine_handle<MyIntFrame>::from_promise(*this);
        // Bind this frame type to Awaitable<int, MyIntFrame>
        return task::Awaitable<int, MyIntFrame>{this};
    }

    ~MyIntFrame() {
        if (has_) std::launder(reinterpret_cast<int*>(&storage_))->~int();
    }
};
```

## Using it
```cpp
task::Awaitable<int, MyIntFrame> compute() {
    co_return 7;
}

task::Awaitable<void> use_it() {
int v = co_await compute(); // works with custom frame
    // ...
}
```
