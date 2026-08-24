#include "test_common.h"
#include "uvent/Uvent.h"
#include "uvent/sync/AsyncUnboundedChannel.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

namespace
{
    void hot_channel_loop_shares_thread()
    {
        constexpr int kIters = 20000;
        usub::Uvent rt(1);
        sync::AsyncUnboundedChannel<int> ch;
        std::atomic<int> hot_progress{0};
        std::atomic<int> hot_progress_when_other_ran{-1};
        std::atomic<bool> other_ran{false};

        auto hot = [&]() -> task::Awaitable<void>
        {
            for (int i = 0; i < kIters; ++i)
            {
                CHECK(ch.try_send(i));
                auto v = co_await ch.recv();
                CHECK(v.has_value());
                hot_progress.store(i + 1, std::memory_order_relaxed);
            }
            rt.stop();
        };
        auto other = [&]() -> task::Awaitable<void>
        {
            other_ran.store(true, std::memory_order_release);
            hot_progress_when_other_ran.store(hot_progress.load(std::memory_order_relaxed), std::memory_order_release);
            co_return;
        };
        system::co_spawn_static(hot(), 0);
        system::co_spawn_static(other(), 0);
        rt.run();
        CHECK(other_ran.load());
        CHECK(hot_progress_when_other_ran.load() < kIters);
    }

    void budget_setting_is_respected()
    {
        const auto saved = settings::coop_budget;
        settings::coop_budget = 16;

        usub::Uvent rt(1);
        sync::AsyncUnboundedChannel<int> ch;
        std::atomic<int> first_observed{-1};
        std::atomic<int> progress{0};

        auto hot = [&]() -> task::Awaitable<void>
        {
            for (int i = 0; i < 1000; ++i)
            {
                CHECK(ch.try_send(i));
                auto v = co_await ch.recv();
                CHECK(v.has_value());
                progress.store(i + 1, std::memory_order_relaxed);
            }
            rt.stop();
        };
        auto probe = [&]() -> task::Awaitable<void>
        {
            first_observed.store(progress.load(std::memory_order_relaxed), std::memory_order_release);
            co_return;
        };
        system::co_spawn_static(hot(), 0);
        system::co_spawn_static(probe(), 0);
        rt.run();
        settings::coop_budget = saved;
        CHECK(first_observed.load() >= 0);
        CHECK(first_observed.load() <= 64);
    }
} // namespace

int main()
{
    return run_tests({
        {"hot_channel_loop_shares_thread", hot_channel_loop_shares_thread},
        {"budget_setting_is_respected", budget_setting_is_respected},
    });
}
