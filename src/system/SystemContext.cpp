//
// Created by kirill on 11/20/24.
//

#include "uvent/system/SystemContext.h"
#include "uvent/tasks/AwaitableFrame.h"

#ifdef OS_LINUX

#include "uvent/poll/EPoller.h"

#elif OS_BSD
#include "poll/KPoller.h"
#else
#error "Windows isn't supported yet"
#endif


namespace usub::uvent::system::this_thread::detail
{
    std::unique_ptr<utils::TimerWheel> wh = std::make_unique<utils::TimerWheel>();
    std::unique_ptr<core::PollerBase> pl =
#ifdef OS_LINUX
        std::make_unique<core::EPoller>(wh.get());
#elif OS_BSD
        new core::KPoller(3);
#else
#error "Windows isn't supported yet"
#endif
    std::unique_ptr<task::SharedTasks> st = std::make_unique<task::SharedTasks>();
    thread_local std::coroutine_handle<> cec{nullptr};
    thread_local std::unique_ptr<queue::single_thread::Queue<std::coroutine_handle<>>> q = std::make_unique<
        queue::single_thread::Queue<std::coroutine_handle<>>>();
    thread_local int t_id{-1};
    thread_local std::unique_ptr<queue::single_thread::Queue<std::coroutine_handle<>>> q_c = std::make_unique<
        queue::single_thread::Queue<std::coroutine_handle<>>>();
    usub::utils::sync::QSBR g_qsbr;
}
