//
// Created by kirill on 8/27/24.
//

#ifndef UVENT_TIMER_H
#define UVENT_TIMER_H

// USUB:
#include "Timer.h"
#include "uvent/system/Defines.h"
#include "uvent/tasks/AwaitableFrame.h"

// STL:
#include <coroutine>
#include <functional>
#include <iostream>
#include <unordered_map>
#if defined(_WIN32)
#include <io.h>      // for _read, _write, _close
#include <process.h> // for _getpid
#include <windows.h> // for Sleep(), GetTickCount(), etc.
#else
#include <unistd.h> // unistd is not stl
#endif
#include "uvent/system/Settings.h"
#include "uvent/utils/datastructures/queue/ConcurrentQueues.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <list>
#include <map>
#include <mutex>
#include <vector>

typedef uint64_t timer_duration_t;
typedef uint64_t timeout_t;

namespace usub::uvent::utils {
class TimerWheel {
public:
  explicit TimerWheel();

  uint64_t addTimer(Timer *timer);

  bool updateTimer(uint64_t timerId, timer_duration_t new_duration);

  bool removeTimer(uint64_t timerId);

  void tick();

  int getNextTimeout() const;

  bool empty() const;

public:
#ifndef UVENT_ENABLE_REUSEADDR
  /**
   * \brief should be locked using lock-free method (e.g try_lock).
   * If it's locked then some thread checks timers.
   * */
  std::mutex mtx;
#endif

private:
  static timeout_t getCurrentTime();

  void addTimerToWheel(Timer *timer, timeout_t expiryTime);

  void removeTimerFromWheel(Timer *timer);

  void advance();

  void updateNextExpiryTime();

  inline static bool is_due(timeout_t now, timeout_t expiry,
                            uint64_t interval) noexcept {
    if (expiry <= now)
      return true;
    const uint64_t diff = expiry - now;
    return diff < interval;
  }

private:
  struct Wheel {
    Wheel(size_t slots, uint64_t interval)
        : slots_(slots), interval_(interval), currentSlot_(0),
          minExpiryTime_(0) {
      buckets_.resize(slots_);
    }

    size_t slots_;
    uint64_t interval_;
    size_t currentSlot_;
    std::vector<std::list<Timer *>> buckets_;

    timeout_t minExpiryTime_;
  };

  std::vector<Wheel> wheels_;
  timeout_t currentTime_;
  std::unordered_map<uint64_t, Timer *> timerMap_;
#ifndef UVENT_ENABLE_REUSEADDR
  std::atomic<uint64_t> timerIdCounter_{0};
#else
  uint64_t timerIdCounter_{0};
#endif
  timeout_t nextExpiryTime_;
  size_t activeTimerCount_;
#ifndef UVENT_ENABLE_REUSEADDR
  queue::concurrent::MPMCQueue<Op> timer_operations_queue;
#else
  queue::single_thread::Queue<Op> timer_operations_queue;
#endif
  std::vector<Op> ops_;
};
} // namespace usub::uvent::utils

#endif // UVENT_TIMER_H
