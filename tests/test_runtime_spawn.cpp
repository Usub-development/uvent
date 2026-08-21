#include "test_common.h"
#include "uvent/Uvent.h"

using namespace usub::uvent;

namespace
{
    std::atomic<uint64_t> g_done{0};

    task::Awaitable<void> tick()
    {
        g_done.fetch_add(1, std::memory_order_relaxed);
        co_return;
    }

    task::Awaitable<void> watcher(usub::Uvent* rt, uint64_t expect)
    {
        while (g_done.load(std::memory_order_acquire) < expect)
            co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(5));
        rt->stop();
        co_return;
    }
} // namespace

int main()
{
    constexpr int threads = 4;
    constexpr uint64_t per_thread_inbox = 50'000;
    constexpr uint64_t shared = 100'000;
    constexpr uint64_t expect = threads * per_thread_inbox + shared;

    usub::Uvent rt(threads);

    std::vector<std::thread> producers;
    for (int t = 0; t < threads; ++t)
        producers.emplace_back([&, t] {
            for (uint64_t i = 0; i < per_thread_inbox; ++i)
                system::co_spawn_static(tick(), t);
        });
    producers.emplace_back([&] {
        for (uint64_t i = 0; i < shared; ++i)
            system::co_spawn(tick());
    });

    system::co_spawn_static(watcher(&rt, expect), 0);

    auto t0 = std::chrono::steady_clock::now();
    rt.run();
    for (auto& p : producers)
        p.join();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    CHECK_EQ(g_done.load(), expect);
    std::printf("[ OK ] runtime_spawn: %llu coroutines in %lld ms\n", (unsigned long long)expect, (long long)ms);
    return 0;
}
