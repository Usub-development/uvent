//
// Created by Kirill on 11.11.2025.
//

#include "uvent/poll/IOCPPoller.h"

#include <mswsock.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#if UVENT_DEBUG
#include "spdlog/spdlog.h"
#endif

#include "uvent/net/Socket.h"
#include "uvent/system/SystemContext.h"
#include "uvent/utils/timer/TimerWheel.h"

namespace usub::uvent::core {

static inline DWORD to_wait_(int timeout_ms) noexcept {
  return (timeout_ms < 0) ? INFINITE : static_cast<DWORD>(timeout_ms);
}

IocpOp IOCPPoller::decode_op_(OVERLAPPED *ov) noexcept {
  auto tag = reinterpret_cast<uintptr_t>(ov);
  if (tag == static_cast<uintptr_t>(IocpOp::WriteKick))
    return IocpOp::WriteKick;

  if (!ov)
    return IocpOp::Unknown;
  auto he = reinterpret_cast<uintptr_t>(ov->hEvent);
  if (he == static_cast<uintptr_t>(IocpOp::Recv0))
    return IocpOp::Recv0;
  if (he == static_cast<uintptr_t>(IocpOp::WriteKick))
    return IocpOp::WriteKick;
  return IocpOp::Unknown;
}

IOCPPoller::IOCPPoller(utils::TimerWheel *wheel) : wheel_(wheel) {
  this->poll_fd = -1;
  iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
  entries_.resize(1024);
}

IOCPPoller::~IOCPPoller() {
  if (iocp_)
    CloseHandle(iocp_);
  iocp_ = nullptr;
}

void IOCPPoller::addEvent(net::SocketHeader *header,
                          OperationType initialState) {
  (void)CreateIoCompletionPort(
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(header->fd)), iocp_,
      reinterpret_cast<ULONG_PTR>(header), 0);
  header->completion_key = reinterpret_cast<ULONG_PTR>(header);

  if (initialState == READ || initialState == ALL)
    arm_recv0_(header);
  if (initialState == WRITE || initialState == ALL)
    kick_write_(header);

#ifndef UVENT_ENABLE_REUSEADDR
  if (header->is_tcp() && header->is_passive())
    system::this_thread::detail::is_started.store(true,
                                                  std::memory_order_relaxed);
#else
  if (header->is_tcp() && header->is_passive())
    system::this_thread::detail::is_started = true;
#endif
}

void IOCPPoller::updateEvent(net::SocketHeader *header,
                             OperationType initialState) {
  if (initialState == READ || initialState == ALL)
    arm_recv0_(header);
  if (initialState == WRITE || initialState == ALL)
    kick_write_(header);
}

void IOCPPoller::removeEvent(net::SocketHeader *header, OperationType) {
#if UVENT_DEBUG
  spdlog::info("Socket removed (IOCP): {}", header->fd);
#endif
  using namespace usub::utils::sync::refc;
  if (header->fd != -1) {
    ::closesocket((SOCKET)header->fd);
    header->fd = -1;
  }
}

void IOCPPoller::arm_recv0_(net::SocketHeader *h) {
  if (h->iocp_read.posted)
    return;
  h->iocp_read.posted = true;

  ZeroMemory(&h->iocp_read.ov, sizeof(h->iocp_read.ov));
  h->iocp_read.ov.hEvent =
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(IocpOp::Recv0));

  h->iocp_read.buf.buf = nullptr;
  h->iocp_read.buf.len = 0;

  DWORD flags = 0;
  DWORD recvd = 0;
  int rc = WSARecv((SOCKET)h->fd, &h->iocp_read.buf, 1, &recvd, &flags,
                   &h->iocp_read.ov, nullptr);
  if (rc == SOCKET_ERROR) {
    int e = WSAGetLastError();
    if (e != WSA_IO_PENDING) {
      h->iocp_read.posted = false;
    }
  }
}

void IOCPPoller::kick_write_(net::SocketHeader *h) {
  if (h->iocp_write.armed)
    return;
  h->iocp_write.armed = true;
  // Синтетическое событие «готов к записи» для совместимости с AwaiterWrite
  PostQueuedCompletionStatus(iocp_, 0, reinterpret_cast<ULONG_PTR>(h),
                             reinterpret_cast<OVERLAPPED *>(
                                 static_cast<uintptr_t>(IocpOp::WriteKick)));
}

bool IOCPPoller::poll(int timeout) {
#ifndef UVENT_ENABLE_REUSEADDR
  system::this_thread::detail::g_qsbr.enter();
#endif
  ULONG n = 0;
  BOOL ok = GetQueuedCompletionStatusEx(iocp_, entries_.data(),
                                        static_cast<ULONG>(entries_.size()), &n,
                                        to_wait_(timeout), FALSE);
  if (!ok) {
    DWORD err = GetLastError();
    if (err == WAIT_TIMEOUT) {
#ifndef UVENT_ENABLE_REUSEADDR
      system::this_thread::detail::g_qsbr.leave();
#endif
      return false;
    }
  }

  for (ULONG i = 0; i < n; ++i) {
    auto &e = entries_[i];
    auto *sock = reinterpret_cast<net::SocketHeader *>(e.lpCompletionKey);
    OVERLAPPED *ov = e.lpOverlapped;
    IocpOp op = decode_op_(ov);

#ifndef UVENT_ENABLE_REUSEADDR
    if (sock->is_busy_now() || sock->is_disconnected_now())
      continue;
    sock->try_mark_busy();
#endif

    bool hup = false;

    if (op == IocpOp::Recv0) {
      // Если peer закрылся — на следующей попытке чтения получим 0
      if (e.dwNumberOfBytesTransferred == 0) {
        char tmp;
        int r = ::recv(sock->fd, &tmp, 1, MSG_PEEK);
        if (r == 0) {
          sock->mark_disconnected();
          hup = true;
        }
      }

      sock->iocp_read.posted = false;

      if (sock->first) {
        auto c = std::exchange(sock->first, nullptr);
        system::this_thread::detail::q->enqueue(c);
      }
      // Переармировать «edge-like»
      if (!hup)
        arm_recv0_(sock);
    } else if (op == IocpOp::WriteKick) {
      sock->iocp_write.armed = false;
      if (sock->second) {
        auto c = std::exchange(sock->second, nullptr);
        system::this_thread::detail::q->enqueue(c);
      }
    }

    if (hup) {
      this->removeEvent(sock, ALL);
    }
  }

  if (n == entries_.size())
    entries_.resize(entries_.size() << 1);

#ifndef UVENT_ENABLE_REUSEADDR
  system::this_thread::detail::g_qsbr.leave();
#endif
  return n > 0;
}

bool IOCPPoller::try_lock() {
  if (this->lock.try_acquire()) {
    this->is_locked.store(true, std::memory_order_release);
    return true;
  }
  return false;
}

void IOCPPoller::unlock() {
  this->is_locked.store(false, std::memory_order_release);
  this->lock.release();
}

void IOCPPoller::lock_poll(int timeout) {
  this->lock.acquire();
  this->is_locked.store(true, std::memory_order_release);
  (void)this->poll(timeout);
  this->unlock();
}

} // namespace usub::uvent::core