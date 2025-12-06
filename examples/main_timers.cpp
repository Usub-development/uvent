//
// Created by root on 12/6/25.
//
#include <source_location>
#include "uvent/Uvent.h"
#include "uvent/sync/AsyncMutex.h"
#include "uvent/sync/AsyncSemaphore.h"
#include "uvent/sync/AsyncEvent.h"
#include "uvent/sync/AsyncWaitGroup.h"
#include "uvent/sync/AsyncCancellation.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

inline std::string make_location_string(const std::source_location& loc = std::source_location::current()) {
    using namespace std::string_literals;
    return std::string(loc.file_name()) + "(" +
           std::to_string(loc.line()) + ":" +
           std::to_string(loc.column()) + ") `" +
           loc.function_name() + "`";
}

void function(std::any value)
{
    spdlog::info("{}: function res = {}", make_location_string(), std::any_cast<int>(value));
}

task::Awaitable<void> coroutine(int a)
{
    spdlog::info("{}: coroutine res = {}", make_location_string(), a);
    co_return;
}

task::Awaitable<void> coroutine_non_arg()
{
    spdlog::info("{}: non_arg coroutine", make_location_string());
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

task::Awaitable<void> coroutine_sleep()
{
    spdlog::info("{}: coroutine_sleep before", make_location_string());
    co_await system::this_coroutine::sleep_for(1000ms);
    spdlog::info("{}: coroutine_sleep after", make_location_string()); // diff 1 second
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

    system::co_spawn_static(function_timer(), 0);
    system::co_spawn_static(coroutine_timer(), 1);
    system::co_spawn_static(coroutine_timer_arg(), 2);
    system::co_spawn_static(coroutine_sleep(), 3);

    uvent.run();
    return 0;
}
