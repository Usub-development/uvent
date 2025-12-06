//
// Created by root on 12/6/25.
//
#include "uvent/Uvent.h"
#include "uvent/sync/AsyncMutex.h"
#include "uvent/sync/AsyncSemaphore.h"
#include "uvent/sync/AsyncEvent.h"
#include "uvent/sync/AsyncWaitGroup.h"
#include "uvent/sync/AsyncCancellation.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

void function(std::any value)
{
    std::cout << std::any_cast<int>(value) << '\n';
}

task::Awaitable<void> coroutine(int a)
{
    std::cout << a << '\n';
    co_return;
}

task::Awaitable<void> coroutine_non_arg()
{
    std::cout << "non_arg" << '\n';
    co_return;
}

task::Awaitable<void> function_timer()
{
    auto timer = new utils::Timer(std::chrono::milliseconds(2000).count());
    timer->addFunction(function, 1);

    system::spawn_timer(timer);
    co_return;
}

task::Awaitable<void> coroutine_timer_arg()
{
    auto timer = new utils::Timer(std::chrono::milliseconds(2000).count());
    timer->addCoroutine(coroutine(2));

    system::spawn_timer(timer);
    co_return;
}

task::Awaitable<void> coroutine_timer()
{
    auto timer = new utils::Timer(std::chrono::milliseconds(2000).count());
    timer->addCoroutine(coroutine_non_arg());

    system::spawn_timer(timer);
    co_return;
}

int main()
{
    settings::timeout_duration_ms = 5000;
#ifdef UVENT_DEBUG
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v%$");
    spdlog::set_level(spdlog::level::trace);
#endif

    usub::Uvent uvent(4);

    system::co_spawn(function_timer());
    system::co_spawn(coroutine_timer());
    system::co_spawn(coroutine_timer_arg());

    uvent.run();
    return 0;
}
