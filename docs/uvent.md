# Uvent

`Uvent` is the main entry point of the runtime.  
It owns the thread pool, drives the event loop, and coordinates coroutine execution.

---

## Class Overview

```cpp
namespace usub {
  class Uvent : std::enable_shared_from_this<Uvent> {
  public:
      explicit Uvent(int threadCount);

      // Iterate over all worker threads before run()
      template <class F>
      void for_each_thread(F&& fn);

      void run();
      void stop();

  private:
      uvent::ThreadPool pool;
  };
}
```

### Constructor

```cpp
explicit Uvent(int threadCount);
```

Creates a runtime with the given number of worker threads.

### for_each_thread

```cpp
template <class F>
void for_each_thread(F&& fn);
```

Invokes `fn(threadIndex, thread::ThreadLocalStorage* tls)` for every worker **before** the event loop starts.
Use it to pre-register per-thread work (e.g., inbox tasks via `co_spawn_static`), initialize TLS, pin resources, etc.

### run

```cpp
void run();
```

Starts the runtime; blocks until `stop()`.

### stop

```cpp
void stop();
```

Signals a graceful shutdown.

---

## Usage Examples

### Global scheduling (simple)

```cpp
int main() {
    usub::Uvent uvent(4);
    system::co_spawn(my_server());   // global queue
    uvent.run();
}
```

### Per-thread scheduling (pre-start)

```cpp
task::Awaitable<void> listeningCoro();

int main() {
    usub::Uvent uvent(4);

    uvent.for_each_thread([&](int threadIndex, thread::ThreadLocalStorage* tls) {
        // enqueue to a specific thread inbox before the loop starts
        system::co_spawn_static(listeningCoro(), threadIndex);
    });

    uvent.run();
}
```

---

## Notes

* Call `for_each_thread` **only before** `run()`.
* Use `co_spawn_static` inside `for_each_thread` to target a specific thread; use `co_spawn` for global scheduling after startup.
* `Uvent` must outlive all scheduled coroutines.