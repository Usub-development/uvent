//
// Created by Kirill Zhukov on 07.11.2024.
//

#include "include/uvent/system/Thread.h"
#include <utility>
#include "include/uvent/net/Socket.h"

namespace usub::uvent::system
{
    Thread::Thread(std::barrier<>* barrier, int index, ThreadLaunchMode tlm)
        : barrier(barrier), index_(
              index), tlm(tlm)
    {
#if UVENT_DEBUG
        spdlog::info("Thread #{} started", index);
#endif
        this_thread::detail::t_id = this->index_;
        this->tmp_tasks_.resize(settings::max_pre_allocated_tasks_items);
        if (tlm == NEW)
            this->thread_ = std::jthread(
                [this](std::stop_token token) { this->threadFunction(token); });
        else this->threadFunction(this->stop_token);
    }

    void Thread::threadFunction(std::stop_token& token)
    {
#if defined(OS_LINUX) && defined(UVENT_PIN_THREADS)
        pthread_t self = pthread_self();
        pin_thread_to_core(this->index_);
        set_thread_name(std::string("Uvent worker " + std::to_string(this->index_)), self);
#endif
        usub::utils::HighPerfTimer highPerfTimer;
        this->barrier->arrive_and_wait();
        using namespace system::this_thread::detail;
        usub::uvent::system::this_thread::detail::g_qsbr.attach_current_thread();
        while (!token.stop_requested())
        {
            if (pl->try_lock())
            {
                auto next_timeout = wh->getNextTimeout();
                pl->poll((q->empty() && utils::detail::thread::is_started.load(std::memory_order_relaxed))
                             ? (next_timeout <= 0)
                                   ? next_timeout
                                   : 5000
                             : 0);
            }
            else if (q->empty() && q_c->empty())
            {
                auto next_timeout = wh->getNextTimeout();
                pl->lock_poll((q->empty() && utils::detail::thread::is_started.load(std::memory_order_relaxed))
                                  ? (next_timeout <= 0)
                                        ? next_timeout
                                        : 5000
                                  : 0);
            }
            highPerfTimer.reset();
            while (true)
            {
                if (q->empty() || highPerfTimer.elapsed_ms() >= 291) break;

                const size_t n = system::this_thread::detail::q->dequeue_bulk(
                    this->tmp_tasks_.data(), this->tmp_tasks_.size());
                if (n == 0) break;
                for (size_t i = 0; i < n; ++i)
                {
                    auto& elem = this->tmp_tasks_[i];
                    if (!elem) continue;

                    auto c = std::coroutine_handle<detail::AwaitableFrameBase>::from_address(elem.address());
                    if (c)
                    {
                        this_thread::detail::cec = c;
#if UVENT_DEBUG
                        spdlog::debug("Prev address: {}", static_cast<void*>(c.address()));
#endif
                        if (!c.done())
                        {
#if UVENT_DEBUG
                            spdlog::info("Coroutine resumed: {}", c.address());
#endif
                            c.resume();
                        }
                    }
                }
            }
            if (wh->mtx.try_lock())
            {
                auto tick_res = wh->tick();
                for (auto& timer : tick_res)
                    system::this_thread::detail::q->enqueue(timer);
                wh->mtx.unlock();
            }
            if (st->getSize() > 0)
            {
                std::coroutine_handle<> task;
                if (st->dequeue(task)) q->enqueue(task);
            }
            highPerfTimer.reset();
            std::coroutine_handle<> c;
            while (q_c->dequeue(c))
            {
                if (highPerfTimer.elapsed_ms() >= 100) break;
                auto c_temp = std::coroutine_handle<detail::AwaitableFrameBase>::from_address(
                    c.address());
#ifdef UVENT_DEBUG
                spdlog::info("Coroutine destroyed in auxiliary loop: {}", c.address());
#endif
                c_temp.destroy();
            }
            usub::uvent::system::this_thread::detail::g_qsbr.quiesce_tick();
        }
        usub::uvent::system::this_thread::detail::g_qsbr.detach_current_thread();
    }

    bool Thread::stop()
    {
        if (this->tlm == NEW)
            return this->thread_.request_stop();
        else
            return this->stop_token.stop_requested();
    }
}
