//
// Created by kirill on 11/20/24.
//

#include "uvent/system/SystemContext.h"
#include "uvent/tasks/AwaitableFrame.h"

#ifdef OS_LINUX
#ifndef UVENT_ENABLE_IO_URING
#include "uvent/poll/EPoller.h"
#else
#include "uvent/poll/IOUringPoller.h"
#endif
#elif OS_BSD || OS_APPLE
#include "uvent/poll/KPoller.h"
#else
#include "uvent/poll/IocpPoller.h"
#endif


namespace usub::uvent::system
{
    namespace this_thread::detail
    {
#ifndef UVENT_ENABLE_REUSEADDR
        utils::TimerWheel wh = utils::TimerWheel();
        core::PollerImpl pl = core::PollerImpl{wh};
#else
        thread_local utils::TimerWheel wh = utils::TimerWheel();
        thread_local core::PollerImpl pl = core::PollerImpl{wh};
#endif
        std::unique_ptr<task::SharedTasks> st = std::make_unique<task::SharedTasks>();
        thread_local std::coroutine_handle<> cec{nullptr};
        thread_local std::unique_ptr<queue::single_thread::Queue<std::coroutine_handle<>>> q = std::make_unique<
            queue::single_thread::Queue<std::coroutine_handle<>>>();
        thread_local int t_id{-1};
        thread_local queue::single_thread::Queue<std::coroutine_handle<>> q_c =
            queue::single_thread::Queue<std::coroutine_handle<>>();
#ifndef UVENT_ENABLE_REUSEADDR
        usub::utils::sync::QSBR g_qsbr;
#else
        thread_local queue::single_thread::Queue<net::SocketHeader*> q_sh =
            queue::single_thread::Queue<net::SocketHeader*>();
#endif
#ifndef UVENT_ENABLE_REUSEADDR
        std::atomic<bool> is_started{false};
#else
        bool is_started{false};
#endif
    }
}
