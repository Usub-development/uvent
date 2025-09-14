# Awaitable

`task::Awaitable<Value, FrameType>` is the coroutine handle type (similar to a `std::future` for coroutines), parameterized by the **promise frame type**.  
By default, `FrameType = detail::AwaitableFrame<Value>`.

```cpp
template<class Value, class FrameType = detail::AwaitableFrame<Value>>
struct Awaitable {
  using promise_type = FrameType;

  bool  await_ready() const noexcept;   // always false → coroutine suspends
  Value await_resume();                 // fetch result via frame_->get()
  template<class U> void await_suspend(std::coroutine_handle<U> h); // link caller
  promise_type* get_promise();          // access underlying promise frame
};
```

There is a specialization for `void`:

```cpp
template<class FrameType>
struct Awaitable<void, FrameType> {
  using promise_type = FrameType;

  bool await_ready() const noexcept;    // always false
  void await_resume();                  // calls frame_->resume()
  template<class U> void await_suspend(std::coroutine_handle<U> h);
  promise_type* get_promise();
};
```

---

## Purpose

* **Separation**: the **handle** (`Awaitable`) is what user code sees, the **promise frame** manages coroutine state and lifecycle.
* **Reusability**: any custom `FrameType` can be plugged in, as long as it inherits from `AwaitableFrameBase` and follows the minimal contract.
* **Integration**: `await_suspend` links caller and callee coroutines, marks the caller as “awaiting,” and ensures proper chaining inside the runtime.

---

## Behavior

* `await_ready()` → always `false`, so `co_await` suspends the caller.
* `await_suspend(h)` → links the awaiting coroutine `h` to the frame:
    * sets the caller as “awaited,”
    * stores `prev_` (and optionally `next_`),
    * the frame then decides scheduling via `initial_suspend` / `final_suspend`.
* `await_resume()`:
    * for `Value` → returns `frame_->get()` (or rethrows if exception stored),
    * for `void` → resumes execution (delegated to the frame).

---

## Custom Frames

`Awaitable` works with **any** `FrameType`, as long as it inherits from `AwaitableFrameBase` and implements:

* `initial_suspend()`, `final_suspend()`
* `get_return_object()` (must return `task::Awaitable<Value, YourFrame>` and set `coro_`)
* `return_value(T)` / `return_void()`
* `get()` (rethrow stored exception if needed)
* `unhandled_exception()`
* correct cleanup in `final_suspend()`:

    * call `push_frame_to_be_destroyed()`,
    * unset caller’s `awaited` flag (`unset_awaited()`),
    * optionally requeue the caller (`push_frame_into_task_queue`).

### Example: custom frame

```cpp
struct MyFrame : detail::AwaitableFrameBase {
  bool has_ = false;
  alignas(int) unsigned char storage_[sizeof(int)];

  std::suspend_always initial_suspend() noexcept { return {}; }

  std::suspend_always final_suspend() noexcept {
    if (prev_) {
      auto caller = std::coroutine_handle<detail::AwaitableFrameBase>::from_address(prev_.address());
      caller.promise().unset_awaited();
      // Optionally: push_frame_into_task_queue(caller);
      prev_ = nullptr;
    }
    push_frame_to_be_destroyed();
    return {};
  }

  void unhandled_exception() { exception_ = std::current_exception(); }
  void return_value(int v) { new (&storage_) int(std::move(v)); has_ = true; }

  int get() {
    if (exception_) std::rethrow_exception(exception_);
    return std::move(*std::launder(reinterpret_cast<int*>(&storage_)));
  }

  auto get_return_object() {
    coro_ = std::coroutine_handle<MyFrame>::from_promise(*this);
    return task::Awaitable<int, MyFrame>{this};
  }

  ~MyFrame() { if (has_) std::launder(reinterpret_cast<int*>(&storage_))->~int(); }
};
```

Usage:

```cpp
task::Awaitable<int, MyFrame> compute() {
  co_return 7;
}

task::Awaitable<void> run() {
  int v = co_await compute(); // works with custom frame
}
```

---

## Common Patterns

**Return a value**

```cpp
task::Awaitable<int> foo() { co_return 42; }
```

**Await inside another coroutine**

```cpp
task::Awaitable<void> bar() {
  int v = co_await foo(); // await_resume() → get()
}
```

**Access promise (rare, runtime use only)**

```cpp
auto* promise = someAwaitable.get_promise(); // FrameType*
```

---

## Key Points

* `Awaitable` is a thin handle; **all logic lives in the frame**.
* Frames must inherit from `AwaitableFrameBase`.
* Exceptions are preserved: `await_resume()` rethrows if `exception_` was set.
* You can define custom scheduling/lifetime semantics via your own frame type.
