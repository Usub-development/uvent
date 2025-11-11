//
// Created by kirill on 11/16/24.
//

#ifndef UVENT_KQUEUEPOLLER_H
#define UVENT_KQUEUEPOLLER_H

#include <csignal>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <io.h>      // for _read, _write, _close
#include <process.h> // for _getpid
#include <windows.h> // for Sleep(), GetTickCount(), etc.
#else
#include <unistd.h>
#endif

#include "PollerBase.h"
#include "uvent/tasks/AwaitableFrame.h"
#include "uvent/utils/timer/TimerWheel.h"

namespace usub::uvent::core {
class KQueuePoller : public PollerBase {
public:
  explicit KQueuePoller(utils::TimerWheel *wheel);

  ~KQueuePoller() override = default;

  void addEvent(net::SocketHeader *header, OperationType initialState) override;

  void updateEvent(net::SocketHeader *header,
                   OperationType initialState) override;

  void removeEvent(net::SocketHeader *header, OperationType op) override;

  bool poll(int timeout_ms) override;

  bool try_lock() override;

  void unlock() override;

  void lock_poll(int timeout_ms) override;

private:
  inline void enable_read(net::SocketHeader *h, bool enable, bool clear_edge) {
    uint16_t flags = (enable ? (EV_ADD | EV_ENABLE) : (EV_ADD | EV_DISABLE));
    if (clear_edge)
      flags |= EV_CLEAR;
    struct kevent ev{};
    EV_SET(&ev, h->fd, EVFILT_READ, flags, 0, 0, h);
    if (kevent(this->poll_fd, &ev, 1, nullptr, 0, nullptr) == -1)
      throw std::system_error(errno, std::generic_category(), "kevent(change)");
  }

  inline void enable_write(net::SocketHeader *h, bool enable, bool clear_edge) {
    uint16_t flags = (enable ? (EV_ADD | EV_ENABLE) : (EV_ADD | EV_DISABLE));
    if (clear_edge)
      flags |= EV_CLEAR;
    struct kevent ev{};
    EV_SET(&ev, h->fd, EVFILT_WRITE, flags, 0, 0, h);
    if (kevent(this->poll_fd, &ev, 1, nullptr, 0, nullptr) == -1)
      throw std::system_error(errno, std::generic_category(), "kevent(change)");
  }

  /// events returned by kevent
  std::vector<struct kevent> events;
  /// mask for signals (держим для совместимости, kqueue сам по себе их не
  /// фильтрует)
  sigset_t sigmask{};
  /// timers storage
  utils::TimerWheel *wheel;
};
} // namespace usub::uvent::core

#endif // UVENT_KQUEUEPOLLER_H
