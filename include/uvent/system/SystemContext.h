//
// Created by kirill on 11/20/24.
//

#ifndef UVENT_SYSTEMCONTEXT_H
#define UVENT_SYSTEMCONTEXT_H

#include <chrono>
#include <memory>
#include <uvent/pool/TLSRegistry.h>
#include "Settings.h"
#include "uvent/base/Predefines.h"
#include "uvent/poll/PollerBase.h"
#include "uvent/tasks/SharedTasks.h"
#include "uvent/utils/datastructures/queue/ConcurrentQueues.h"
#include "uvent/utils/datastructures/queue/FastQueue.h"
#include "uvent/utils/sync/QSBR.h"
#include "uvent/utils/timer/TimerWheel.h"

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
    /// \attention **Do not attempt to modify variables inside directly** unless explicitly instructed in the
    /// documentation.
    namespace this_thread::detail
    {
#ifndef UVENT_ENABLE_REUSEADDR
        /// \brief Wrapper over I/O notification mechanism provided by OS.
        extern std::unique_ptr<core::PollerBase> pl;
        /// \brief Timer wheel used to handle multiple timers efficiently.
        extern std::unique_ptr<utils::TimerWheel> wh;
#else
        /// \brief Wrapper over I/O notification mechanism provided by OS.
        thread_local extern core::PollerImpl pl;
        /// \brief Timer wheel used to handle multiple timers efficiently.
        thread_local extern utils::TimerWheel wh;
#endif
        /// \brief Task queue available to all threads in the thread pool.
        /// Or available to a single thread if there is only one thread in the thread pool
        extern std::unique_ptr<task::SharedTasks> st;
        /// \brief Currently executing coroutine (cec).
        thread_local extern std::coroutine_handle<> cec;
        /// \brief Thread's index inside thread pool.
        thread_local extern int t_id;
        /// \brief Coroutines to be destroyed
        thread_local extern queue::single_thread::Queue<std::coroutine_handle<>> q_c;
#ifndef UVENT_ENABLE_REUSEADDR
        extern usub::utils::sync::QSBR g_qsbr;
#else
        /// \brief Sockets to be destroyed
        thread_local extern queue::single_thread::Queue<net::SocketHeader*> q_sh;
#endif

#ifndef UVENT_ENABLE_REUSEADDR
        extern std::atomic<bool> is_started;
#else
        extern bool is_started;
#endif
    } // namespace this_thread::detail

    namespace this_coroutine
    {
        template <class Rep, class Period>
        task::Awaitable<void> sleep_for(std::chrono::duration<Rep, Period> d)
        {
            using namespace std::chrono;
            auto ms = duration_cast<milliseconds>(d + milliseconds(1) - milliseconds(0));
            auto ms_count = std::max<int64_t>(1, ms.count());

            struct SleepAwaiter
            {
                utils::Timer* t;
                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> h) const noexcept
                {
                    t->bind(h);
                    this_thread::detail::wh.addTimer(t);
                }

                void await_resume() const noexcept {}
            };

            co_await SleepAwaiter{new utils::Timer(static_cast<timer_duration_t>(ms_count))};
        }
    } // namespace this_coroutine

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
        if (promise)
            this_thread::detail::st->enqueue(promise->get_coroutine_handle());
    }

    /**
     * @brief Enqueues a coroutine into the inbox of a specific thread before the event loop starts.
     *
     * Registers a coroutine for execution in the context of the given thread prior to system startup.
     * The coroutine handle is placed into the target thread’s inbox queue.
     *
     * @tparam F Type providing `get_promise()` returning an optional-like pointer to a promise.
     * @param f Coroutine function/object to be enqueued.
     * @param threadIndex Index of the target thread whose inbox receives the coroutine.
     *
     * @note This function does not validate that the event loop is not started. The caller must ensure
     *       it is used only before global event loop initialization. Use `co_spawn()` after startup.
     */
    template <typename F>
    void co_spawn_static(F&& f, int threadIndex)
    {
        auto promise = f.get_promise();
        if (promise)
            global::detail::tls_registry->getStorage(threadIndex)->push_task_inbox(promise->get_coroutine_handle());
    }

    /**
     * @brief Enqueues an existing coroutine handle into the inbox of a specific thread before the event loop starts.
     *
     * Places the provided coroutine handle into the target thread’s inbox queue for later execution.
     *
     * @param h Coroutine handle to be enqueued.
     * @param threadIndex Index of the target thread whose inbox receives the coroutine.
     *
     * @note This function does not validate that the event loop is not started. The caller must ensure
     *       it is used only before global event loop initialization. Use `co_spawn()` after startup.
     *
     * @warning This function does not take ownership of the coroutine lifetime beyond storing the handle.
     *          The coroutine must remain valid until executed/destroyed by the runtime.
     */
    inline void co_spawn_static(std::coroutine_handle<> h, int threadIndex)
    {
        global::detail::tls_registry->getStorage(threadIndex)->push_task_inbox(h);
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
    inline void spawn_timer(utils::Timer* timer) { this_thread::detail::wh.addTimer(timer); }
} // namespace usub::uvent::system


#endif // UVENT_SYSTEMCONTEXT_H
