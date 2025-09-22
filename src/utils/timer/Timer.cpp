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

    void Timer::addFunction(std::function<void(void*)>& function, void* functionValue)
    {
        auto aw = timeout_coroutine(std::move(function), functionValue);
        this->coro = aw.get_promise()->get_coroutine_handle();
    }

    void Timer::addFunction(std::function<void(void*)>&& function, void* functionValue)
    {
        auto aw = timeout_coroutine(std::move(function), functionValue);
        this->coro = aw.get_promise()->get_coroutine_handle();
    }
}
