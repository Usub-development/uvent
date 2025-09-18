//
// Created by kirill on 11/20/24.
//

#ifndef UVENT_SYSTEMCONTEXT_H
#define UVENT_SYSTEMCONTEXT_H

#include <chrono>
#include <memory>
#include "uvent/tasks/SharedTasks.h"
#include "uvent/utils/timer/TimerWheel.h"
#include "Settings.h"
#include "uvent/utils/datastructures/queue/FastQueue.h"
#include "uvent/utils/datastructures/queue/ConcurrentQueues.h"
#include "uvent/utils/sync/QSBR.h"
#include "uvent/poll/PollerBase.h"
#include "uvent/base/Predefines.h"

namespace usub::uvent::system
{
    /// \brief Variables used internally within the system.
    /// \attention **Do not attempt to modify variables inside directly** unless explicitly instructed in the documentation.
    namespace this_thread::detail
    {
        /// \brief Wrapper over I/O notification mechanism provided by OS.
        extern std::unique_ptr<core::PollerBase> pl;
        /// \brief Timer wheel used to handle multiple timers efficiently.
        extern std::unique_ptr<utils::TimerWheel> wh;
        /// \brief Task queue available to all threads in the thread pool.
        /// Or available to a single thread if there is only one thread in the thread pool
        extern std::unique_ptr<task::SharedTasks> st;
        /// \brief Currently executing coroutine (cec).
        thread_local extern std::coroutine_handle<> cec;
        /// \brief Thread's index inside thread pool.
        thread_local extern int t_id;
        /// \brief Coroutines to be destroyed
        thread_local extern std::unique_ptr<queue::single_thread::Queue<std::coroutine_handle<>>> q_c;
        extern usub::utils::sync::QSBR g_qsbr;
    }

    namespace this_coroutine
    {
        template <typename Rep, typename Period>
        task::Awaitable<void> sleep_for(const std::chrono::duration<Rep, Period>& sleep_duration)
        {
            auto timer = new utils::Timer(sleep_duration.count());
            timer->coro = this_thread::detail::cec;
            this_thread::detail::wh->addTimer(timer);
            return {};
        }

        template <typename Rep, typename Period>
        task::Awaitable<void> sleep_for(const std::chrono::duration<Rep, Period>&& sleep_duration)
        {
            auto timer = new utils::Timer(sleep_duration.count());
            timer->coro = this_thread::detail::cec;
            this_thread::detail::wh->addTimer(timer);
            return {};
        }
    }

    template <typename F>
    void co_spawn(F&& f)
    {
        auto promise = f.get_promise();
        if (promise) system::this_thread::detail::st->enqueue(promise->get_coroutine_handle());
    }
}


#endif //UVENT_SYSTEMCONTEXT_H
