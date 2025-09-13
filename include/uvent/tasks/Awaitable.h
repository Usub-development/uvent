//
// Created by Kirill Zhukov on 07.11.2024.
//

#ifndef UVENT_AWAITABLE_H
#define UVENT_AWAITABLE_H

#include <queue>
#include <cstdint>
#include <iostream>
#include <coroutine>
#include <exception>
#include "include/uvent/base/Predefines.h"

#ifdef UVENT_DEBUG

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#endif

namespace usub::uvent::task
{
    template <class Value, class FrameType = detail::AwaitableFrame<Value>>
    struct Awaitable
    {
    public:
        using promise_type = FrameType;

        template <typename>
        friend
        class detail::AwaitableFrame;

        template <typename>
        friend
        class detail::AwaitableIOFrame;

        Awaitable() = default;

        virtual ~Awaitable() = default;

        [[nodiscard]] bool await_ready() const noexcept;

        virtual Value await_resume();

#if !defined(UVENT_NEW_STACK)

        template <class U>
        void await_suspend(std::coroutine_handle<U> h);

#else
            template<class U>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<U> h);
#endif
        promise_type* get_promise();

        /// @brief Should be used carefully! Only for `get_return_object` in promise type.
        explicit Awaitable(promise_type* af);

    protected:
        promise_type* frame_{nullptr};
    };

    template <class FrameType>
    struct Awaitable<void, FrameType>
    {
    public:
        using promise_type = FrameType;

        template <typename>
        friend
        class detail::AwaitableFrame;

        template <typename>
        friend
        class AwaitableIOFrame;

        Awaitable() = default;

        ~Awaitable() = default;

        [[nodiscard]] bool await_ready() const noexcept;

        virtual void await_resume();

        promise_type* get_promise();

#if !defined(UVENT_NEW_STACK)

        template <class U>
        void await_suspend(std::coroutine_handle<U> h);

#else
            template<class U>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<U> h);
#endif
        /// @brief Should be used carefully! Only for `get_return_object` in promise type.
        explicit Awaitable(promise_type* af);

    protected:
        promise_type* frame_{nullptr};
    };

    template <class Value, class FrameType>
    Value Awaitable<Value, FrameType>::await_resume()
    {
        return this->frame_->get();
    }

    template <class Value, class FrameType>
    bool Awaitable<Value, FrameType>::await_ready() const noexcept
    {
        return false;
    }

    template <class Value, class FrameType>
    Awaitable<Value, FrameType>::Awaitable(promise_type* af) : frame_(af)
    {
    }

    template <class Value, class FrameType>
    typename Awaitable<Value, FrameType>::promise_type* Awaitable<Value, FrameType>::get_promise()
    {
        return this->frame_;
    }
}

#endif //UVENT_AWAITABLE_H
