//
// Created by root on 9/9/25.
//

#ifndef TIMER_H
#define TIMER_H

#include <cstdint>
#include <unordered_map>
#include <functional>
#include <coroutine>
#include <any>

#include "uvent/base/Predefines.h"
#include "uvent/system/Defines.h"
#include "uvent/tasks/AwaitableFrame.h"

typedef uint64_t timer_duration_t;
typedef uint64_t timeout_t;

namespace usub::uvent::utils
{
    enum TimerType
    {
        TIMEOUT,
        INTERVAL
    };

    task::Awaitable<void> timeout_coroutine(std::function<void(std::any&)> f, std::any arg);

    class alignas(32) Timer
    {
    public:
        friend class core::EPoller;
        friend class TimerWheel;

        explicit Timer(timer_duration_t duration, TimerType type = TIMEOUT);

        Timer(const Timer&) = delete;

        Timer& operator=(const Timer&) = delete;

        Timer(Timer&&) = delete;

        Timer& operator=(Timer&&) = delete;

        void addFunction(std::function<void(std::any&)> f, std::any arg);

        void addFunction(std::function<void(std::any&)> f, std::any& arg);

        void bind(std::coroutine_handle<> h) noexcept;

    public:
        timeout_t expiryTime;
        timer_duration_t duration_ms;
        TimerType type;

    private:
        std::coroutine_handle<> coro;
        bool active;
        uint64_t id;
        size_t slotIndex{0};
        size_t level{0};
    };

    enum class OpType : uint8_t { ADD, UPDATE, REMOVE };

    struct alignas(16) Op
    {
        OpType op;
        uint8_t _pad[7];

        union
        {
            struct
            {
                Timer* timer;
                uint64_t _unused0;
            };

            struct
            {
                uint64_t id;
                uint64_t new_dur;
            };

            struct
            {
                uint64_t id_only;
                uint64_t _unused1;
            };
        };
    };

    static_assert(std::is_trivially_copyable_v<Op>, "Op must be POD");
    static_assert(sizeof(Op) == 32, "Expect 32 bytes");
}

#endif //TIMER_H
