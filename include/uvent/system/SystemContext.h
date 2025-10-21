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
#include <uvent/pool/TLSRegistry.h>

namespace usub::uvent::system
{
#ifndef UVENT_ENABLE_REUSEADDR
    constexpr bool is_reuseaddr_enabled = false;
#else
    constexpr bool is_reuseaddr_enabled = true;
#endif

    namespace global::detail
    {
        inline std::unique_ptr<thread::TLSRegistry> tls_registry{nullptr};
    }

    /// \brief Variables used internally within the system.
    /// \attention **Do not attempt to modify variables inside directly** unless explicitly instructed in the documentation.
    namespace this_thread::detail
    {
#ifndef UVENT_ENABLE_REUSEADDR
        /// \brief Wrapper over I/O notification mechanism provided by OS.
        extern std::unique_ptr<core::PollerBase> pl;
        /// \brief Timer wheel used to handle multiple timers efficiently.
        extern std::unique_ptr<utils::TimerWheel> wh;
#else
        /// \brief Wrapper over I/O notification mechanism provided by OS.
        thread_local extern std::unique_ptr<core::PollerBase> pl;
        /// \brief Timer wheel used to handle multiple timers efficiently.
        thread_local extern std::unique_ptr<utils::TimerWheel> wh;
#endif
        /// \brief Task queue available to all threads in the thread pool.
        /// Or available to a single thread if there is only one thread in the thread pool
        extern std::unique_ptr<task::SharedTasks> st;
        /// \brief Currently executing coroutine (cec).
        thread_local extern std::coroutine_handle<> cec;
        /// \brief Thread's index inside thread pool.
        thread_local extern int t_id;
        /// \brief Coroutines to be destroyed
        thread_local extern std::unique_ptr<queue::single_thread::Queue<std::coroutine_handle<>>> q_c;
#ifndef UVENT_ENABLE_REUSEADDR
        extern usub::utils::sync::QSBR g_qsbr;
#else
        /// \brief Sockets to be destroyed
        thread_local extern std::unique_ptr<queue::single_thread::Queue<net::SocketHeader*>> q_sh;
#endif

#ifndef UVENT_ENABLE_REUSEADDR
        extern std::atomic<bool> is_started;
#else
        extern bool is_started;
#endif
    }

    namespace this_coroutine
    {
        template <typename Rep, typename Period>
        task::Awaitable<void> sleep_for(const std::chrono::duration<Rep, Period>& sleep_duration)
        {
            using namespace std::chrono;
            const auto ms = duration_cast<milliseconds>(sleep_duration).count();

            struct SleepAwaiter
            {
                utils::Timer* t;
                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    this->t->bind(h);
                    this_thread::detail::wh->addTimer(this->t);
                }

                void await_resume() const noexcept
                {
                }
            };

            co_await SleepAwaiter{new utils::Timer(static_cast<timer_duration_t>(ms), utils::TimerType::TIMEOUT)};
        }

        template <typename Rep, typename Period>
        task::Awaitable<void> sleep_for(const std::chrono::duration<Rep, Period>&& sleep_duration)
        {
            using namespace std::chrono;
            const auto ms = duration_cast<milliseconds>(sleep_duration).count();

            struct SleepAwaiter
            {
                utils::Timer* t;
                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    this->t->bind(h);
                    this_thread::detail::wh->addTimer(this->t);
                }

                void await_resume() const noexcept
                {
                }
            };

            co_await SleepAwaiter{new utils::Timer(static_cast<timer_duration_t>(ms), utils::TimerType::TIMEOUT)};
        }
    }

    /**
     * @brief Spawns a coroutine for execution in the global thread context.
     *
     * Retrieves the coroutine promise from the given function object and, if valid,
     * enqueues its coroutine handle into the global task queue.
     *
     * @tparam F Coroutine function type providing `get_promise()`.
     * @param f Coroutine function to be spawned.
     *
     * @warning Method doesn't check if the coroutine is valid beyond `get_promise()`.
     *          Ensure the coroutine object remains valid until scheduled.
     */
    template <typename F>
    void co_spawn(F&& f)
    {
        auto promise = f.get_promise();
        if (promise) this_thread::detail::st->enqueue(promise->get_coroutine_handle());
    }

    /**
     * @brief Enqueues a coroutine into the inbox of a specific thread before the event loop starts.
     *
     * Used to register coroutines for execution in a given thread's context prior to system startup.
     * The coroutine handle obtained from the function's promise is placed directly into the target
     * thread’s inbox queue.
     *
     * @tparam F Type of the coroutine function providing `get_promise()`.
     * @param f Coroutine function to be enqueued.
     * @param threadIndex Index of the target thread whose inbox receives the coroutine.
     *
     * @throws std::runtime_error If the system event loop has already started.
     *
     * @note This function must be called only before the global event loop initialization.
     *       Use `co_spawn()` after the system is started.
     */
    template <typename F>
    void co_spawn_static(F&& f, int threadIndex)
    {
        if (!this_thread::detail::is_started)
        {
            auto promise = f.get_promise();
            if (promise)
                global::detail::tls_registry->getStorage(threadIndex)->push_task_inbox(
                    promise->get_coroutine_handle());
        }
        else
        {
            throw std::runtime_error("co_spawn_static: uvent already started. use co_spawn instead.");
        }
    }

    /**
     * @brief Schedules a timer in timer wheel.
     *
     * Adds the given timer instance into timer wheel handler,
     * allowing it to be triggered after its configured expiry.
     *
     * @param timer Pointer to a valid timer object.
     *
     * @note If the timer type is set to TIMEOUT, it will fire once;
     *       otherwise, it will repeat indefinitely.
     *
     * @warning This method does not check whether the timer is initialized
     *          or already active. Use only with properly constructed and inactive timers.
     */
    inline void spawn_timer(utils::Timer* timer)
    {
        this_thread::detail::wh->addTimer(timer);
    }
}


#endif //UVENT_SYSTEMCONTEXT_H
