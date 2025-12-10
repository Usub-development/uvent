#include <iostream>
#include <chrono>

#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"
#include "uvent/sync/AsyncChannel.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

static sync::AsyncChannel<int> g_ch{4};

task::Awaitable<void> producer()
{
    for (int i = 0; i < 10; ++i)
    {
        bool ok = co_await g_ch.send(i);
        if (!ok)
        {
            std::cout << "[producer] channel closed at i=" << i << "\n";
            co_return;
        }
        std::cout << "[producer] sent " << i << "\n";
        co_await system::this_coroutine::sleep_for(100ms);
    }

    std::cout << "[producer] closing channel\n";
    g_ch.close();
    co_return;
}

task::Awaitable<void> consumer(int id)
{
    for (;;)
    {
        auto opt = co_await g_ch.recv();
        if (!opt.has_value())
        {
            std::cout << "[consumer " << id << "] channel closed, exit\n";
            co_return;
        }

        auto& [v] = *opt;

        std::cout << "[consumer " << id << "] got " << v << "\n";
        co_await system::this_coroutine::sleep_for(250ms);
    }
}

int main()
{
#ifdef UVENT_DEBUG
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v%$");
    spdlog::set_level(spdlog::level::trace);
#endif

    usub::Uvent uvent(4);

    system::co_spawn_static(producer(), 0);
    system::co_spawn_static(consumer(1), 1);
    system::co_spawn_static(consumer(2), 2);

    uvent.run();
    return 0;
}
