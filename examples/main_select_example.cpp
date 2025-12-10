#include <iostream>
#include <chrono>

#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"
#include "uvent/sync/AsyncChannel.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

using usub::uvent::sync::AsyncChannel;
using usub::uvent::sync::select_recv;

static AsyncChannel<int> g_ch1{4};
static AsyncChannel<int> g_ch2{4};

task::Awaitable<void> producer1()
{
    for (int i = 0; i < 10; ++i)
    {
        bool ok = co_await g_ch1.send(i);
        if (!ok)
        {
            std::cout << "[producer1] channel closed at i=" << i << "\n";
            co_return;
        }
        std::cout << "[producer1] sent " << i << "\n";
        co_await system::this_coroutine::sleep_for(100ms);
    }

    std::cout << "[producer1] closing ch1\n";
    g_ch1.close();
    co_return;
}

task::Awaitable<void> producer2()
{
    for (int i = 0; i < 10; ++i)
    {
        int v = 100 + i;
        bool ok = co_await g_ch2.send(v);
        if (!ok)
        {
            std::cout << "[producer2] channel closed at i=" << i << "\n";
            co_return;
        }
        std::cout << "[producer2] sent " << v << "\n";
        co_await system::this_coroutine::sleep_for(150ms);
    }

    std::cout << "[producer2] closing ch2\n";
    g_ch2.close();
    co_return;
}

task::Awaitable<void> select_consumer()
{
    for (;;)
    {
        auto res = co_await select_recv(g_ch1, g_ch2);
        if (!res)
        {
            std::cout << "[select] all channels closed, exit\n";
            co_return;
        }

        auto [idx, tup] = *res;
        auto& [v] = tup;

        std::cout << "[select] from ch" << (idx + 1) << ": " << v << "\n";
    }
}

int main()
{
#ifdef UVENT_DEBUG
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v%$");
    spdlog::set_level(spdlog::level::trace);
#endif

    usub::Uvent uvent(4);

    system::co_spawn_static(producer1(), 0);
    system::co_spawn_static(producer2(), 1);
    system::co_spawn_static(select_consumer(), 2);

    uvent.run();
    return 0;
}
