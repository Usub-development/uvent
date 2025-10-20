//
// Created by root on 9/9/25.
//

#include "uvent/utils/timer/Timer.h"

namespace usub::uvent::utils
{
    Timer::Timer(timer_duration_t duration, TimerType type) : duration_ms(duration), type(type),
                                                              expiryTime(0),
                                                              active(true), id(0)
    {
    }

    void Timer::addFunction(std::function<void(std::any&)> f, std::any& arg)
    {
        auto aw = timeout_coroutine(std::move(f), arg);
        this->coro = aw.get_promise()->get_coroutine_handle();
    }

    void Timer::addFunction(std::function<void(std::any&)> f, std::any&& arg)
    {
        auto aw = timeout_coroutine(std::move(f), arg);
        this->coro = aw.get_promise()->get_coroutine_handle();
    }
}
