#include <string>

#include "test_common.h"
#include "uvent/Uvent.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

#ifdef UVENT_TASK_INTROSPECTION

namespace
{
    std::atomic<bool> g_checked{false};

    task::Awaitable<void> named_sleeper()
    {
        system::this_coroutine::set_trace_id(0xDEAD);
        co_await system::this_coroutine::sleep_for(60s);
    }

    task::Awaitable<void> inspector(usub::Uvent* rt)
    {
        auto h = task::spawn(named_sleeper(), 1);
        co_await system::this_coroutine::sleep_for(100ms);

        auto tasks = introspection::snapshot();
        CHECK(tasks.size() >= 2);

        bool found_sleep = false;
        for (const auto& t : tasks)
        {
            if (t.wait_reason && std::string(t.wait_reason) == "sleep" && t.trace_id == 0xDEAD)
                found_sleep = true;
        }
        CHECK(found_sleep);
        CHECK(introspection::live_count() >= tasks.size() - 1);
        introspection::dump(stdout);

        h.cancel();
        co_await h;
        g_checked.store(true, std::memory_order_release);
        rt->stop();
    }

    void snapshot_sees_sleeping_task()
    {
        usub::Uvent rt(2);
        system::co_spawn_static(inspector(&rt), 0);
        rt.run();
        CHECK(g_checked.load());
    }

    void frames_unregister_on_completion()
    {
        const auto before = introspection::live_count();
        {
            usub::Uvent rt(2);
            auto body = [&]() -> task::Awaitable<void> {
                co_await system::this_coroutine::sleep_for(1ms);
                rt.stop();
            };
            system::co_spawn_static(body(), 0);
            rt.run();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(introspection::live_count() <= before + 4);
    }
} // namespace

int main()
{
    return run_tests({
        {"snapshot_sees_sleeping_task", snapshot_sees_sleeping_task},
        {"frames_unregister_on_completion", frames_unregister_on_completion},
    });
}

#else

int main()
{
    std::printf("skipped: build with -DUVENT_TASK_INTROSPECTION=ON\n");
    return 0;
}

#endif
