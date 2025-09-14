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

      void run();
      void stop();

  private:
      uvent::ThreadPool pool;
  };
}
````

* **Constructor**

  ```cpp
  explicit Uvent(int threadCount);
  ```

  Creates a runtime with the given number of worker threads.
  Each thread runs the poller, timer wheel, and shared task queue.

* **run()**

  ```cpp
  void run();
  ```

  Starts the runtime: worker threads are launched, the event loop is entered, and scheduled coroutines begin executing.
  This call blocks until `stop()` is invoked.

* **stop()**

  ```cpp
  void stop();
  ```

  Signals the runtime to shut down gracefully.
  Worker threads exit the event loop, the thread pool joins, and resources are released.

---

## Usage Example

```cpp
int main() {
    // create runtime with 4 threads
    usub::Uvent uvent(4);

    // schedule some coroutine(s)
    system::co_spawn(my_server());

    // start event loop (blocks until stop() is called)
    uvent.run();
}
```

---

## Notes

* `Uvent` must exist for the lifetime of scheduled coroutines.
* Always use `co_spawn` to enqueue work into the runtime before calling `run()`.
* `stop()` can be called from inside a coroutine or from external control logic to terminate the loop.