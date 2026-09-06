#include <chrono>
#include <cstdio>

#include "uvent/Uvent.h"
#include "uvent/sync/AsyncUnboundedChannel.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

namespace
{
    sync::AsyncUnboundedChannel<uint64_t> g_jobs;

    task::Awaitable<uint64_t> worker(int id)
    {
        system::this_coroutine::set_name("worker");
        uint64_t processed = 0;
        for (;;)
        {
            auto r = co_await sync::select(g_jobs.recv_op(), sync::sleep_op(200ms));
            if (r.cancelled())
                break;
            if (r.is<1>())
            {
                std::printf("[worker %d] idle for 200ms, %llu processed so far\n", id,
                            static_cast<unsigned long long>(processed));
                continue;
            }
            auto& item = r.get<0>();
            if (!item.has_value())
                break;
            processed += std::get<0>(*item);
        }
        std::printf("[worker %d] done, processed sum = %llu\n", id, static_cast<unsigned long long>(processed));
        co_return processed;
    }

    task::Awaitable<void> producer()
    {
        for (uint64_t i = 1; i <= 100; ++i)
        {
            g_jobs.try_send(i);
            if (i % 25 == 0)
            {
                if (!co_await system::this_coroutine::sleep_for(50ms))
                    co_return;
            }
        }
        g_jobs.close();
    }

    task::Awaitable<void> stubborn()
    {
        system::this_coroutine::set_name("stubborn");
        while (!system::this_coroutine::cancel_requested())
        {
            if (!co_await system::this_coroutine::sleep_for(10ms))
                break;
        }
        std::printf("[stubborn] observed cancellation, exiting cleanly\n");
    }

    task::Awaitable<void> supervisor(usub::Uvent* rt)
    {
        system::this_coroutine::set_trace_id(0xC0FFEE);

        task::TaskScope scope;
        auto w0 = scope.spawn(worker(0), 0);
        auto w1 = scope.spawn(worker(1), 1);
        scope.spawn(producer(), 0);
        scope.spawn(stubborn(), 1);

        co_await system::this_coroutine::sleep_for(300ms);

        introspection::dump(stdout);

        co_await scope.cancel_and_join();

        const uint64_t s0 = co_await w0;
        const uint64_t s1 = co_await w1;
        std::printf("[supervisor] workers consumed %llu + %llu = %llu\n", static_cast<unsigned long long>(s0),
                    static_cast<unsigned long long>(s1), static_cast<unsigned long long>(s0 + s1));

        rt->stop();
    }
} // namespace

int main()
{
    usub::Uvent rt(2);
    system::co_spawn_static(supervisor(&rt), 0);
    rt.run();
    return 0;
}
