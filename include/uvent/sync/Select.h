#ifndef UVENT_SYNC_SELECT_H
#define UVENT_SYNC_SELECT_H

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "uvent/sync/AsyncCancellation.h"
#include "uvent/sync/SyncCommon.h"
#include "uvent/sync/Wait.h"
#include "uvent/sync/WaitList.h"
#include "uvent/system/SystemContext.h"
#include "uvent/utils/timer/Timer.h"

namespace usub::uvent::sync
{
    struct SleepOp
    {
        uint64_t duration_ms;
        utils::Timer* t{nullptr};
        uint64_t id{0};
        std::atomic<Waiter*> waiter{nullptr};
        std::atomic<bool> fired{false};
        std::atomic<bool> fire_done{false};

        using result_type = std::monostate;

        static const char* wait_reason() noexcept { return "sleep"; }

        SleepOp(const SleepOp& o) noexcept : duration_ms(o.duration_ms) {}

        SleepOp& operator=(const SleepOp&) = delete;

        explicit SleepOp(uint64_t ms) noexcept : duration_ms(ms) {}

        bool try_complete(std::monostate&) const noexcept { return this->fired.load(std::memory_order_acquire); }

        bool attach(Waiter* w) noexcept
        {
            this->waiter.store(w, std::memory_order_release);
            if (!this->t)
            {
                this->t = new utils::Timer(static_cast<timer_duration_t>(this->duration_ms));
                this->t->arm_raw(static_cast<timer_duration_t>(this->duration_ms), &SleepOp::on_fire, this);
                this->id = system::this_thread::detail::wh.addTimer(this->t);
            }
            return !this->fired.load(std::memory_order_acquire);
        }

        bool detach(Waiter* w) noexcept
        {
            Waiter* prev = this->waiter.exchange(nullptr, std::memory_order_acq_rel);
            if (prev == w)
                return true;
            if (prev == nullptr && !this->fired.load(std::memory_order_acquire))
                return true;
            while (!this->fire_done.load(std::memory_order_acquire))
                cpu_relax();
            return false;
        }

        void finalize() noexcept
        {
            if (!this->t)
                return;
            if (system::detail::cancel_timer_now(this->id))
            {
                delete this->t;
                this->t = nullptr;
                return;
            }
            while (!this->fire_done.load(std::memory_order_acquire))
                cpu_relax();
            this->t = nullptr;
        }

        static void on_fire(void* arg) noexcept
        {
            auto* s = static_cast<SleepOp*>(arg);
            s->fired.store(true, std::memory_order_release);
            Waiter* w = s->waiter.exchange(nullptr, std::memory_order_acq_rel);
            if (w)
                detail::fire_waiter(w);
            s->fire_done.store(true, std::memory_order_release);
        }
    };

    template <class Rep, class Period>
    SleepOp sleep_op(std::chrono::duration<Rep, Period> d) noexcept
    {
        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(d + milliseconds(1) - milliseconds(0));
        return SleepOp(static_cast<uint64_t>(std::max<int64_t>(1, ms.count())));
    }

    template <class... Ops>
    struct SelectResult
    {
        static constexpr int32_t cancelled_index = -1;

        int32_t index{cancelled_index};
        std::variant<std::monostate, typename Ops::result_type...> value{};

        [[nodiscard]] bool cancelled() const noexcept { return this->index < 0; }

        template <std::size_t I>
        [[nodiscard]] auto& get()
        {
            return std::get<I + 1>(this->value);
        }

        template <std::size_t I>
        [[nodiscard]] bool is() const noexcept
        {
            return this->index == static_cast<int32_t>(I);
        }
    };

    namespace detail
    {
        template <std::size_t I, class Tuple, class Result>
        bool select_try_one(Tuple& t, Result& r)
        {
            auto& op = std::get<I>(t);
            r.value.template emplace<I + 1>();
            if (op.try_complete(std::get<I + 1>(r.value)))
            {
                r.index = static_cast<int32_t>(I);
                return true;
            }
            return false;
        }

        template <class Tuple, class Result, std::size_t... Is>
        bool select_try_round(Tuple& t, Result& r, uint32_t start, std::index_sequence<Is...>)
        {
            constexpr uint32_t N = sizeof...(Is);
            for (uint32_t k = 0; k < N; ++k)
            {
                const uint32_t i = (start + k) % N;
                bool done = false;
                ((Is == i ? (done = select_try_one<Is>(t, r)) : false), ...);
                if (done)
                    return true;
            }
            return false;
        }

        template <class Tuple, std::size_t N>
        struct SelectSuspend
        {
            Tuple* t;
            Waiter* nodes;
            std::atomic<int32_t>* winner;
            bool own_cancel{false};

            bool await_ready() const noexcept { return false; }

            template <std::size_t... Is>
            bool attach_all(std::coroutine_handle<> h, int tid, std::index_sequence<Is...>) noexcept
            {
                bool ok = true;
                ((ok &&
                  (this->nodes[Is].reset(h, tid), this->nodes[Is].winner = this->winner,
                   this->nodes[Is].index = static_cast<int32_t>(Is),
                   ok = std::get<Is>(*this->t).attach(&this->nodes[Is]))),
                 ...);
                return ok;
            }

            template <std::size_t... Is>
            void detach_all(std::index_sequence<Is...>) noexcept
            {
                (std::get<Is>(*this->t).detach(&this->nodes[Is]), ...);
            }

            bool await_suspend(std::coroutine_handle<> h) noexcept
            {
                auto* f = &uvent::detail::frame_of(h);
                if (f->cancel_requested())
                {
                    this->own_cancel = true;
                    return false;
                }
                const int tid = current_thread_id();
                if (!this->attach_all(h, tid, std::make_index_sequence<N>{}))
                {
                    this->detach_all(std::make_index_sequence<N>{});
                    return this->winner->load(std::memory_order_acquire) != -1;
                }
                if (f->arm_cancel(&SelectSuspend::on_cancel, this, "select"))
                {
                    int32_t expected = -1;
                    if (this->winner->compare_exchange_strong(expected, Waiter::kCancelledIndex,
                                                              std::memory_order_acq_rel, std::memory_order_relaxed))
                    {
                        f->disarm_cancel();
                        this->detach_all(std::make_index_sequence<N>{});
                        this->own_cancel = true;
                        return false;
                    }
                }
                return true;
            }

            bool await_resume() noexcept
            {
                if (this->own_cancel)
                    return false;
                this->detach_all(std::make_index_sequence<N>{});
                return this->winner->load(std::memory_order_relaxed) != Waiter::kCancelledIndex;
            }

            static void on_cancel(uvent::detail::AwaitableFrameBase* f, void* arg) noexcept
            {
                auto* s = static_cast<SelectSuspend*>(arg);
                int32_t expected = -1;
                if (s->winner->compare_exchange_strong(expected, Waiter::kCancelledIndex, std::memory_order_acq_rel,
                                                       std::memory_order_relaxed))
                    system::this_thread::detail::q.enqueue(f->get_coroutine_handle());
            }
        };
    } // namespace detail

    template <class... Ops>
    task::Awaitable<SelectResult<std::remove_cvref_t<Ops>...>> select(Ops... ops)
    {
        using Result = SelectResult<std::remove_cvref_t<Ops>...>;
        constexpr std::size_t N = sizeof...(Ops);
        static_assert(N >= 1, "select: need at least one operation");

        Result res;
        auto tuple = std::tie(ops...);
        Waiter nodes[N];
        std::atomic<int32_t> winner{-1};
        const uint32_t start = detail::select_rotation() % N;

        for (;;)
        {
            if (detail::select_try_round(tuple, res, start, std::make_index_sequence<N>{}))
                break;
            winner.store(-1, std::memory_order_relaxed);
            detail::SelectSuspend<decltype(tuple), N> aw{&tuple, nodes, &winner};
            if (!co_await aw)
            {
                res.index = Result::cancelled_index;
                res.value.template emplace<0>();
                break;
            }
        }
        (ops.finalize(), ...);
        if (!system::coop::consume())
            co_await system::this_coroutine::yield();
        co_return res;
    }
} // namespace usub::uvent::sync

#endif // UVENT_SYNC_SELECT_H
