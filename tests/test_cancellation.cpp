#include "test_common.h"
#include "uvent/Uvent.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

namespace
{
    void source_token_basics()
    {
        sync::CancellationSource src;
        auto tok = src.token();
        CHECK(!tok.stop_requested());
        src.request_cancel();
        CHECK(tok.stop_requested());
        auto tok2 = tok;
        CHECK(tok2.stop_requested());
    }

    void tree_propagation()
    {
        sync::CancellationSource parent;
        sync::CancellationSource child{parent.token()};
        sync::CancellationSource grandchild{child.token()};
        CHECK(!grandchild.token().stop_requested());
        parent.request_cancel();
        CHECK(child.token().stop_requested());
        CHECK(grandchild.token().stop_requested());
    }

    void child_of_cancelled_is_born_cancelled()
    {
        sync::CancellationSource parent;
        parent.request_cancel();
        sync::CancellationSource child{parent.token()};
        CHECK(child.token().stop_requested());
    }

    std::atomic<bool> g_on_cancel_fired{false};

    task::Awaitable<void> on_cancel_waiter(usub::Uvent* rt, sync::CancellationToken tok)
    {
        const bool fired = co_await tok.on_cancel();
        CHECK(fired);
        g_on_cancel_fired.store(true, std::memory_order_release);
        rt->stop();
    }

    void on_cancel_wakeup()
    {
        usub::Uvent rt(2);
        sync::CancellationSource src;
        system::co_spawn_static(on_cancel_waiter(&rt, src.token()), 0);
        std::thread killer(
            [&]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                src.request_cancel();
            });
        rt.run();
        killer.join();
        CHECK(g_on_cancel_fired.load());
    }

    std::atomic<bool> g_sleep_cancelled{false};

    task::Awaitable<void> long_sleeper()
    {
        const bool completed = co_await system::this_coroutine::sleep_for(60s);
        if (!completed)
            g_sleep_cancelled.store(true, std::memory_order_release);
    }

    task::Awaitable<void> sleep_cancel_driver(usub::Uvent* rt)
    {
        auto handle = task::spawn(long_sleeper());
        co_await system::this_coroutine::sleep_for(50ms);
        handle.cancel();
        co_await handle;
        CHECK(g_sleep_cancelled.load(std::memory_order_acquire));
        rt->stop();
    }

    void sleep_cancellation_is_prompt()
    {
        usub::Uvent rt(2);
        system::co_spawn_static(sleep_cancel_driver(&rt), 0);
        auto t0 = std::chrono::steady_clock::now();
        rt.run();
        auto elapsed = std::chrono::steady_clock::now() - t0;
        CHECK(elapsed < 5s);
    }

    std::atomic<int> g_loop_ticks{0};

    task::Awaitable<void> cooperative_loop()
    {
        while (!system::this_coroutine::cancel_requested())
        {
            g_loop_ticks.fetch_add(1, std::memory_order_relaxed);
            if (!co_await system::this_coroutine::sleep_for(5ms))
                break;
        }
    }

    task::Awaitable<void> loop_cancel_driver(usub::Uvent* rt)
    {
        auto handle = task::spawn(cooperative_loop(), 1);
        co_await system::this_coroutine::sleep_for(100ms);
        handle.cancel();
        co_await handle;
        rt->stop();
    }

    void cross_thread_task_cancel()
    {
        usub::Uvent rt(2);
        system::co_spawn_static(loop_cancel_driver(&rt), 0);
        rt.run();
        CHECK(g_loop_ticks.load() >= 1);
    }
} // namespace

int main()
{
    return run_tests({
        {"source_token_basics", source_token_basics},
        {"tree_propagation", tree_propagation},
        {"child_of_cancelled_is_born_cancelled", child_of_cancelled_is_born_cancelled},
        {"on_cancel_wakeup", on_cancel_wakeup},
#ifdef UVENT_ENABLE_REUSEADDR
        {"sleep_cancellation_is_prompt", sleep_cancellation_is_prompt},
#endif
        {"cross_thread_task_cancel", cross_thread_task_cancel},
    });
}
