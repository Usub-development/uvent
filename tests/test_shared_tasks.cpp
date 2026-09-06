#include "test_common.h"
#include "uvent/tasks/SharedTasks.h"
#include "uvent/utils/datastructures/queue/FastQueue.h"

using namespace usub::uvent::task;

namespace
{
    std::coroutine_handle<> fake(uintptr_t i) { return std::coroutine_handle<>::from_address(reinterpret_cast<void*>(i)); }

    void no_loss_above_old_capacity()
    {
        SharedTasks st;
        constexpr uintptr_t n = 200'000;
        for (uintptr_t i = 1; i <= n; ++i)
            st.enqueue(fake(i * 16));
        CHECK_EQ(st.getSize(), n);
        std::coroutine_handle<> h;
        for (uintptr_t i = 1; i <= n; ++i)
        {
            CHECK(st.dequeue(h));
            CHECK_EQ(reinterpret_cast<uintptr_t>(h.address()), i * 16);
        }
        CHECK(!st.dequeue(h));
    }

    void bulk_drain_into_local_queue()
    {
        SharedTasks st;
        usub::queue::single_thread::Queue<std::coroutine_handle<>> local;
        for (uintptr_t i = 1; i <= 5000; ++i)
            st.enqueue(fake(i * 16));
        size_t drained = 0;
        while (st.dequeue_bulk(&local))
        {
            std::coroutine_handle<> h;
            while (local.dequeue(h))
            {
                ++drained;
                CHECK_EQ(reinterpret_cast<uintptr_t>(h.address()), drained * 16);
            }
        }
        CHECK_EQ(drained, 5000u);
    }

    void concurrent_spawn_and_steal()
    {
        SharedTasks st;
        std::atomic<uint64_t> stolen{0};
        std::atomic<bool> stop{false};
        std::vector<std::thread> th;
        for (int p = 0; p < 6; ++p)
            th.emplace_back([&] {
                for (uintptr_t i = 1; i <= 100000; ++i)
                    st.enqueue(fake(i * 16));
            });
        for (int c = 0; c < 6; ++c)
            th.emplace_back([&] {
                usub::queue::single_thread::Queue<std::coroutine_handle<>> local;
                uint64_t n = 0;
                std::coroutine_handle<> h;
                auto drain = [&] {
                    while (local.dequeue(h))
                        ++n;
                };
                while (!stop.load(std::memory_order_acquire))
                    if (st.dequeue_bulk(&local))
                        drain();
                while (st.dequeue_bulk(&local))
                    drain();
                stolen.fetch_add(n);
            });
        for (int p = 0; p < 6; ++p)
            th[p].join();
        stop.store(true, std::memory_order_release);
        for (int c = 6; c < 12; ++c)
            th[c].join();
        CHECK_EQ(stolen.load(), 600000u);
    }
} // namespace

int main()
{
    return run_tests({
        {"no_loss_above_old_capacity", no_loss_above_old_capacity},
        {"bulk_drain_into_local_queue", bulk_drain_into_local_queue},
        {"concurrent_spawn_and_steal", concurrent_spawn_and_steal},
    });
}
