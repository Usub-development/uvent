//
// Created by root on 9/9/25.
//

#include "uvent/utils/timer/Timer.h"

namespace usub::uvent::utils
{
    Timer::Timer(timer_duration_t duration) :
        duration_ms(duration),
        expiryTime(0),
        active(true), id(0)
    {
    }

    void Timer::addFunction(std::function<void(std::any&)> f, std::any arg)
    {
        auto aw = timeout_coroutine(std::move(f), arg);
        this->coro = aw.get_promise()->get_coroutine_handle();
    }

    void Timer::addFunction(std::function<void(std::any&)> f, std::any& arg)
    {
        auto aw = timeout_coroutine(std::move(f), arg);
        this->coro = aw.get_promise()->get_coroutine_handle();
    }

    void Timer::bind(std::coroutine_handle<> h) noexcept
    {
        this->coro = h;
        this->active = true;
    }

    void Timer::arm_embedded(timer_duration_t dur, raw_timer_fn f, void* arg) noexcept
    {
        this->duration_ms = dur;
        this->heap = false;
        this->raw_fn = f;
        this->raw_arg = arg;
        this->coro = nullptr;
        this->active = true;
    }
}
