//
// Created by Kirill Zhukov on 07.11.2024.
//

#ifndef UVENT_AWAITABLE_H
#define UVENT_AWAITABLE_H

#include <coroutine>
#include <cstdint>
#include <exception>
#include <iostream>
#include <queue>

#include "uvent/base/Predefines.h"

#ifdef UVENT_DEBUG

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#endif

namespace usub::uvent::task {
    template <class Value, class FrameType = detail::AwaitableFrame<Value>>
    struct Awaitable {
       public:
        using promise_type = FrameType;

        template <typename>
        friend class detail::AwaitableFrame;

        template <typename>
        friend class detail::AwaitableIOFrame;

        Awaitable() = default;

        virtual ~Awaitable() = default;

        [[nodiscard]] bool await_ready() const noexcept;

        virtual Value await_resume();

        template <class U>
        void await_suspend(std::coroutine_handle<U> h);

        promise_type* get_promise();

        /// @brief Should be used carefully! Only for `get_return_object` in promise type.
        explicit Awaitable(promise_type* af);

        bool is_done() const;

       protected:
        promise_type* frame_{nullptr};
    };

    template <class FrameType>
    struct Awaitable<void, FrameType> {
       public:
        using promise_type = FrameType;

        template <typename>
        friend class detail::AwaitableFrame;

        template <typename>
        friend class AwaitableIOFrame;

        Awaitable() = default;

        virtual ~Awaitable() = default;

        [[nodiscard]] bool await_ready() const noexcept;

        virtual void await_resume();

        promise_type* get_promise();

        template <class U>
        void await_suspend(std::coroutine_handle<U> h);

        /// @brief Should be used carefully! Only for `get_return_object` in promise type.
        explicit Awaitable(promise_type* af);

        bool is_done() const;

       protected:
        promise_type* frame_{nullptr};
    };

    template <class Value, class FrameType>
    Value Awaitable<Value, FrameType>::await_resume() {
        return this->frame_->get();
    }

    template <class Value, class FrameType>
    bool Awaitable<Value, FrameType>::await_ready() const noexcept {
        return !frame_ || frame_->get_coroutine_handle().done();
    }

    template <class Value, class FrameType>
    Awaitable<Value, FrameType>::Awaitable(promise_type* af) : frame_(af) {}

    template <class Value, class FrameType>
    typename Awaitable<Value, FrameType>::promise_type* Awaitable<Value, FrameType>::get_promise() {
        return this->frame_;
    }

    template <class Value, class FrameType>
    bool Awaitable<Value, FrameType>::is_done() const {
        return this->frame_->get_coroutine_handle().done();
    }

    template <class FrameType>
    bool Awaitable<void, FrameType>::is_done() const {
        return this->frame_->get_coroutine_handle().done();
    }
}  // namespace usub::uvent::task

#endif  // UVENT_AWAITABLE_H
