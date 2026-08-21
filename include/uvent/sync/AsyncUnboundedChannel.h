//
// Created by kirill on 21/08/26.
//

#ifndef UVENT_SYNC_ASYNC_UNBOUNDED_CHANNEL_H
#define UVENT_SYNC_ASYNC_UNBOUNDED_CHANNEL_H

#include <atomic>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "uvent/sync/AsyncChannel.h"
#include "uvent/sync/AsyncEvent.h"
#include "uvent/tasks/AwaitableFrame.h"
#include "uvent/utils/datastructures/queue/ConcurrentQueues.h"

namespace usub::uvent::sync
{
    template <class... Ts>
    class AsyncUnboundedChannel
    {
    public:
        using value_type = std::tuple<std::decay_t<Ts>...>;

    private:
        using Queue = usub::queue::concurrent::SegmentedMPMCQueue<value_type>;

        Queue queue_;
        AsyncEvent can_recv_{Reset::Manual, false};
        std::atomic<bool> closed_{false};

        void notify_recv() noexcept
        {
            can_recv_.set();
            g_select_recv_event.set();
        }

    public:
        AsyncUnboundedChannel() = default;

        AsyncUnboundedChannel(const AsyncUnboundedChannel&) = delete;
        AsyncUnboundedChannel& operator=(const AsyncUnboundedChannel&) = delete;

        AsyncUnboundedChannel(AsyncUnboundedChannel&&) = delete;
        AsyncUnboundedChannel& operator=(AsyncUnboundedChannel&&) = delete;

        bool is_closed() const noexcept { return closed_.load(std::memory_order_acquire); }

        void close() noexcept
        {
            if (closed_.exchange(true, std::memory_order_acq_rel))
                return;
            notify_recv();
        }

        [[nodiscard]] std::size_t size_relaxed() const noexcept { return queue_.size_relaxed(); }
        [[nodiscard]] bool empty_relaxed() const noexcept { return queue_.empty_relaxed(); }

        template <class... Us>
        bool try_send(Us&&... vs)
        {
            static_assert(sizeof...(Ts) == sizeof...(Us), "try_send: argument count mismatch");
            if (is_closed())
                return false;
            queue_.emplace(std::forward<Us>(vs)...);
            notify_recv();
            return true;
        }

        bool try_send_tuple(const value_type& v)
        {
            if (is_closed())
                return false;
            queue_.enqueue(v);
            notify_recv();
            return true;
        }

        bool try_send_tuple(value_type&& v)
        {
            if (is_closed())
                return false;
            queue_.enqueue(std::move(v));
            notify_recv();
            return true;
        }

        template <class... Us>
        task::Awaitable<bool> send(Us&&... vs)
        {
            static_assert(sizeof...(Ts) == sizeof...(Us), "send: argument count mismatch");
            co_return try_send(std::forward<Us>(vs)...);
        }

        task::Awaitable<bool> send_tuple(value_type v) { co_return try_send_tuple(std::move(v)); }

        bool try_recv(value_type& out) { return queue_.try_dequeue(out); }

        template <class... Us>
        bool try_recv_into(Us&... out)
        {
            static_assert(sizeof...(Ts) == sizeof...(Us), "try_recv_into: argument count mismatch");
            value_type tmp;
            if (!queue_.try_dequeue(tmp))
                return false;
            assign_from_tuple(tmp, out...);
            return true;
        }

        task::Awaitable<std::optional<value_type>> recv()
        {
            value_type tmp;
            for (;;)
            {
                if (queue_.try_dequeue(tmp))
                    co_return std::optional<value_type>{std::move(tmp)};

                if (is_closed() && queue_.empty_relaxed())
                    co_return std::nullopt;

                can_recv_.reset();

                if (queue_.try_dequeue(tmp))
                    co_return std::optional<value_type>{std::move(tmp)};

                if (is_closed() && queue_.empty_relaxed())
                    co_return std::nullopt;

                co_await can_recv_.wait();
            }
        }

        template <class... Us>
        task::Awaitable<bool> recv_into(Us&... out)
        {
            static_assert(sizeof...(Ts) == sizeof...(Us), "recv_into: argument count mismatch");
            auto r = co_await recv();
            if (!r)
                co_return false;
            assign_from_tuple(*r, out...);
            co_return true;
        }

    private:
        template <class Tup, class... Us, std::size_t... Is>
        static void assign_from_tuple_impl(Tup& t, std::tuple<Us&...> refs, std::index_sequence<Is...>)
        {
            ((std::get<Is>(refs) = std::move(std::get<Is>(t))), ...);
        }

        template <class... Us>
        static void assign_from_tuple(value_type& t, Us&... out)
        {
            std::tuple<Us&...> refs{out...};
            assign_from_tuple_impl(t, refs, std::index_sequence_for<Us...>{});
        }
    };

    template <class T>
    inline task::Awaitable<bool> operator<<(AsyncUnboundedChannel<T>& ch, T v)
    {
        co_return co_await ch.send(std::move(v));
    }

    template <class... Ts, class... Us>
    inline task::Awaitable<bool> operator<<(AsyncUnboundedChannel<Ts...>& ch, std::tuple<Us...> v)
    {
        static_assert(sizeof...(Ts) == sizeof...(Us), "operator<<: tuple size mismatch");
        co_return co_await ch.send_tuple(std::move(v));
    }
} // namespace usub::uvent::sync

#endif // UVENT_SYNC_ASYNC_UNBOUNDED_CHANNEL_H
