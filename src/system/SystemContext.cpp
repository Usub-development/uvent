//
// Created by kirill on 11/20/24.
//

#include "uvent/system/SystemContext.h"
#include "uvent/tasks/AwaitableFrame.h"

#ifdef OS_LINUX

#include "uvent/poll/EPoller.h"

#elif OS_BSD || OS_APPLE
#include "uvent/poll/KPoller.h"
#else
#include "uvent/poll/IocpPoller.h"
#endif


namespace usub::uvent::system {
    namespace global::detail {
    }

    namespace this_thread::detail {
#ifndef UVENT_ENABLE_REUSEADDR
        std::unique_ptr<utils::TimerWheel> wh = std::make_unique<utils::TimerWheel>();
        std::unique_ptr<core::PollerBase> pl =
#ifdef OS_LINUX
        std::make_unique<core::EPoller> (wh.get());
#elif OS_BSD
        new core::KPoller (3);
#else
#error "Windows isn't supported yet"
#endif
#else
        thread_local std::unique_ptr<utils::TimerWheel> wh = std::make_unique<utils::TimerWheel>();
        thread_local std::unique_ptr<core::PollerBase> pl =
#ifdef OS_LINUX
                std::make_unique<core::EPoller>(wh.get());
#elif OS_BSD || OS_APPLE
                std::make_unique<core::KQueuePoller>(wh.get());
#else
                std::make_unique<core::IocpPoller>(wh.get());
#endif
#endif
        std::unique_ptr<task::SharedTasks> st = std::make_unique<task::SharedTasks>();
        thread_local std::coroutine_handle<> cec{nullptr};
        thread_local std::unique_ptr<queue::single_thread::Queue<std::coroutine_handle<> > > q = std::make_unique<
            queue::single_thread::Queue<std::coroutine_handle<> > >();
        thread_local int t_id{-1};
        thread_local std::unique_ptr<queue::single_thread::Queue<std::coroutine_handle<> > > q_c = std::make_unique<
            queue::single_thread::Queue<std::coroutine_handle<> > >();
#ifndef UVENT_ENABLE_REUSEADDR
        usub::utils::sync::QSBR g_qsbr;
#else
        thread_local std::unique_ptr<queue::single_thread::Queue<net::SocketHeader *> > q_sh = std::make_unique<
            queue::single_thread::Queue<net::SocketHeader *> >();
#endif
#ifndef UVENT_ENABLE_REUSEADDR
        std::atomic<bool> is_started{false};
#else
        bool is_started{false};
#endif
    }
}
