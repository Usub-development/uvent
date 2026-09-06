//
// Created by root on 9/9/25.
//

#ifndef TIMER_H
#define TIMER_H

#include <any>
#include <coroutine>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <type_traits>
#include <unordered_map>

#include "uvent/base/Predefines.h"
#include "uvent/system/Defines.h"
#include "uvent/tasks/AwaitableFrame.h"

typedef uint64_t timer_duration_t;
typedef uint64_t timeout_t;

namespace usub::uvent::utils
{
    task::Awaitable<void> timeout_coroutine(std::function<void(std::any&)> f, std::any arg);

    using raw_timer_fn = void (*)(void*);

    class alignas(32) Timer
    {
    public:
        friend class core::EPoller;
        friend class TimerWheel;

        explicit Timer(timer_duration_t duration);

        Timer(const Timer&) = delete;

        Timer& operator=(const Timer&) = delete;

        Timer(Timer&&) = delete;

        Timer& operator=(Timer&&) = delete;

        void addFunction(std::function<void(std::any&)> f, std::any arg);

        void addFunction(std::function<void(std::any&)> f, std::any& arg);

        template <class AwaitableT>
        void addCoroutine(AwaitableT&& aw)
        {
            using A = std::remove_reference_t<AwaitableT>;

            static_assert(
                requires(A a) { a.get_promise(); },
                "Timer::addCoroutine expects usub::uvent::task::Awaitable<>-like type");

            auto* p = aw.get_promise();
            this->coro = p->get_coroutine_handle();
            this->active = true;
        }

        void bind(std::coroutine_handle<> h) noexcept;

        void arm_embedded(timer_duration_t dur, raw_timer_fn f, void* arg) noexcept;

        void arm_raw(timer_duration_t dur, raw_timer_fn f, void* arg) noexcept;

    public:
        timeout_t expiryTime;
        timer_duration_t duration_ms;

    private:
        std::coroutine_handle<> coro;
        raw_timer_fn raw_fn{nullptr};
        void* raw_arg{nullptr};
        bool heap{true};
        bool active;
        uint64_t id;
        size_t slotIndex{0};
        size_t level{0};
        size_t posInBucket{0};
    };

    enum class OpType : uint8_t
    {
        ADD,
        UPDATE,
        REMOVE
    };

    struct alignas(16) Op
    {
        OpType op;
        uint8_t _pad[7];

        union
        {
            struct
            {
                Timer* timer;
                /// id snapshot taken at addTimer(): lets the wheel drop a queued ADD whose
                /// timer was cancelled meanwhile without dereferencing `timer` (it may be
                /// embedded in a SocketHeader that is already gone)
                uint64_t add_id;
            };

            struct
            {
                uint64_t id;
                uint64_t new_dur;
            };

            struct
            {
                uint64_t id_only;
                /// REMOVE: argument for `done`
                void* done_arg;
            };
        };

        /// REMOVE only: called by the wheel right after the node has been dropped
        /// (found & removed, already fired, or queued-ADD cancelled). Lets the owner
        /// of an embedded Timer free its enclosing object only when the wheel can no
        /// longer touch it (non-REUSEADDR: SocketHeader retired via QSBR from here).
        raw_timer_fn done{nullptr};
    };

    static_assert(std::is_trivially_copyable_v<Op>, "Op must be POD");
    static_assert(sizeof(Op) == 32, "Expect 32 bytes");
} // namespace usub::uvent::utils

#endif // TIMER_H
