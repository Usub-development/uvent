#include "test_common.h"
#include "uvent/Uvent.h"
#include "uvent/sync/AsyncEvent.h"
#include "uvent/sync/AsyncMutex.h"
#include "uvent/sync/AsyncSemaphore.h"
#include "uvent/sync/AsyncWaitGroup.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

namespace
{
    void manual_event_wakes_all()
    {
        usub::Uvent rt(2);
        sync::AsyncEvent ev{sync::Reset::Manual};
        std::atomic<int> woken{0};
        constexpr int kWaiters = 16;

        auto waiter = [&]() -> task::Awaitable<void> {
            CHECK(co_await ev.wait());
            if (woken.fetch_add(1, std::memory_order_acq_rel) + 1 == kWaiters)
                rt.stop();
        };
        for (int i = 0; i < kWaiters; ++i)
            system::co_spawn_static(waiter(), i % 2);

        auto setter = [&]() -> task::Awaitable<void> {
            co_await system::this_coroutine::sleep_for(50ms);
            ev.set();
        };
        system::co_spawn_static(setter(), 0);
        rt.run();
        CHECK_EQ(woken.load(), kWaiters);
    }

    void auto_event_serial_handoff()
    {
        usub::Uvent rt(2);
        sync::AsyncEvent ev{sync::Reset::Auto};
        std::atomic<int> woken{0};
        constexpr int kRounds = 8;

        auto waiter = [&]() -> task::Awaitable<void> {
            for (int i = 0; i < kRounds; ++i)
            {
                CHECK(co_await ev.wait());
                woken.fetch_add(1, std::memory_order_relaxed);
            }
            rt.stop();
        };
        system::co_spawn_static(waiter(), 0);

        auto setter = [&]() -> task::Awaitable<void> {
            for (int i = 0; i < kRounds; ++i)
            {
                co_await system::this_coroutine::sleep_for(5ms);
                ev.set();
            }
        };
        system::co_spawn_static(setter(), 1);
        rt.run();
        CHECK_EQ(woken.load(), kRounds);
    }

    void semaphore_bounds_concurrency()
    {
        usub::Uvent rt(4);
        sync::AsyncSemaphore sem{2};
        std::atomic<int> inside{0};
        std::atomic<int> max_inside{0};
        std::atomic<int> finished{0};
        constexpr int kWorkers = 12;

        auto worker = [&]() -> task::Awaitable<void> {
            CHECK(co_await sem.acquire());
            const int now = inside.fetch_add(1, std::memory_order_acq_rel) + 1;
            int prev = max_inside.load(std::memory_order_relaxed);
            while (prev < now && !max_inside.compare_exchange_weak(prev, now)) {}
            co_await system::this_coroutine::sleep_for(10ms);
            inside.fetch_sub(1, std::memory_order_acq_rel);
            sem.release();
            if (finished.fetch_add(1, std::memory_order_acq_rel) + 1 == kWorkers)
                rt.stop();
        };
        for (int i = 0; i < kWorkers; ++i)
            system::co_spawn_static(worker(), i % 4);
        rt.run();
        CHECK_EQ(finished.load(), kWorkers);
        CHECK(max_inside.load() <= 2);
    }

    void waitgroup_waits_for_all()
    {
        usub::Uvent rt(2);
        sync::WaitGroup wg;
        std::atomic<int> done{0};
        constexpr int kJobs = 10;
        wg.add(kJobs);

        auto job = [&](int ms) -> task::Awaitable<void> {
            co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(ms));
            done.fetch_add(1, std::memory_order_relaxed);
            wg.done();
        };
        auto waiter = [&]() -> task::Awaitable<void> {
            CHECK(co_await wg.wait());
            CHECK_EQ(done.load(), kJobs);
            rt.stop();
        };
        system::co_spawn_static(waiter(), 0);
        for (int i = 0; i < kJobs; ++i)
        {
            auto j = job(2 + i);
            system::co_spawn_static(std::move(j), i % 2);
        }
        rt.run();
        CHECK_EQ(done.load(), kJobs);
    }

    void mutex_mutual_exclusion()
    {
        usub::Uvent rt(4);
        sync::AsyncMutex mtx;
        uint64_t counter = 0;
        std::atomic<int> finished{0};
        constexpr int kWorkers = 8;
        constexpr int kIters = 2000;

        auto worker = [&]() -> task::Awaitable<void> {
            for (int i = 0; i < kIters; ++i)
            {
                auto g = co_await mtx.lock();
                CHECK(g.owns_lock());
                ++counter;
            }
            if (finished.fetch_add(1, std::memory_order_acq_rel) + 1 == kWorkers)
                rt.stop();
        };
        for (int i = 0; i < kWorkers; ++i)
            system::co_spawn_static(worker(), i % 4);
        rt.run();
        CHECK_EQ(counter, uint64_t(kWorkers) * kIters);
    }

    void mutex_cancelled_waiter_gets_empty_guard()
    {
        usub::Uvent rt(2);
        sync::AsyncMutex mtx;
        std::atomic<bool> waiter_cancelled{false};

        auto blocked = [&]() -> task::Awaitable<void> {
            auto g = co_await mtx.lock();
            waiter_cancelled.store(!g.owns_lock(), std::memory_order_release);
        };
        auto driver = [&]() -> task::Awaitable<void> {
            auto g = co_await mtx.lock();
            CHECK(g.owns_lock());
            auto h = task::spawn(blocked(), 1);
            co_await system::this_coroutine::sleep_for(50ms);
            h.cancel();
            co_await h;
            CHECK(waiter_cancelled.load(std::memory_order_acquire));
            g.unlock();
            rt.stop();
        };
        system::co_spawn_static(driver(), 0);
        auto t0 = std::chrono::steady_clock::now();
        rt.run();
        CHECK(std::chrono::steady_clock::now() - t0 < 5s);
    }
} // namespace

int main()
{
    return run_tests({
        {"manual_event_wakes_all", manual_event_wakes_all},
        {"auto_event_serial_handoff", auto_event_serial_handoff},
        {"semaphore_bounds_concurrency", semaphore_bounds_concurrency},
        {"waitgroup_waits_for_all", waitgroup_waits_for_all},
        {"mutex_mutual_exclusion", mutex_mutual_exclusion},
#ifdef UVENT_ENABLE_REUSEADDR
        {"mutex_cancelled_waiter_gets_empty_guard", mutex_cancelled_waiter_gets_empty_guard},
#endif
    });
}
