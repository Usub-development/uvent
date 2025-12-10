//
// Created by kirill on 12/10/25.
//

#ifndef UVENT_SYNC_ASYNC_CHANNEL_H
#define UVENT_SYNC_ASYNC_CHANNEL_H

#include <atomic>
#include <tuple>
#include <optional>
#include <utility>
#include <type_traits>

#include "uvent/system/Settings.h"
#include "uvent/sync/AsyncEvent.h"
#include "uvent/utils/datastructures/queue/ConcurrentQueues.h"
#include "uvent/tasks/AwaitableFrame.h"

namespace usub::uvent::sync
{
    template <class... Ts>
    class AsyncChannel
    {
    public:
        using value_type = std::tuple<std::decay_t<Ts>...>;

    private:
        using Queue = usub::queue::concurrent::MPMCQueue<value_type>;

        Queue queue_;
        AsyncEvent can_recv_{Reset::Auto, false};
        AsyncEvent can_send_{Reset::Auto, false};
        std::atomic<bool> closed_{false};

        friend task::Awaitable<
            std::optional<std::pair<std::size_t, value_type>>>
        select_recv<>(AsyncChannel&);

    public:
        explicit AsyncChannel(std::size_t capacity_pow2 = 1024) :
            queue_(capacity_pow2)
        {
        }

        AsyncChannel(const AsyncChannel&) = delete;
        AsyncChannel& operator=(const AsyncChannel&) = delete;

        AsyncChannel(AsyncChannel&&) = delete;
        AsyncChannel& operator=(AsyncChannel&&) = delete;

        bool is_closed() const noexcept
        {
            return closed_.load(std::memory_order_acquire);
        }

        void close() noexcept
        {
            bool was = closed_.exchange(true, std::memory_order_acq_rel);
            if (was)
                return;

            can_recv_.set();
            can_send_.set();
            extern AsyncEvent g_select_recv_event;
            g_select_recv_event.set();
        }

        std::size_t capacity() const noexcept { return queue_.capacity(); }
        std::size_t size_relaxed() const noexcept { return queue_.size_relaxed(); }
        bool empty_relaxed() const noexcept { return queue_.empty_relaxed(); }

        template <class... Us>
        bool try_send(Us&&... vs)
        {
            static_assert(sizeof...(Ts) == sizeof...(Us),
                          "try_send: argument count mismatch");
            value_type v{std::forward<Us>(vs)...};
            if (!queue_.try_enqueue(v))
                return false;
            can_recv_.set();
            extern AsyncEvent g_select_recv_event;
            g_select_recv_event.set();
            return true;
        }

        bool try_send_tuple(const value_type& v)
        {
            if (!queue_.try_enqueue(v))
                return false;
            can_recv_.set();
            extern AsyncEvent g_select_recv_event;
            g_select_recv_event.set();
            return true;
        }

        bool try_send_tuple(value_type&& v)
        {
            if (!queue_.try_enqueue(std::move(v)))
                return false;
            can_recv_.set();
            extern AsyncEvent g_select_recv_event;
            g_select_recv_event.set();
            return true;
        }

        bool try_recv(value_type& out)
        {
            if (!queue_.try_dequeue(out))
                return false;
            can_send_.set();
            return true;
        }

        template <class... Us>
        bool try_recv_into(Us&... out)
        {
            static_assert(sizeof...(Ts) == sizeof...(Us),
                          "try_recv_into: argument count mismatch");
            value_type tmp;
            if (!queue_.try_dequeue(tmp))
                return false;

            assign_from_tuple(tmp, out...);
            can_send_.set();
            return true;
        }

        template <class... Us>
        task::Awaitable<bool> send(Us&&... vs)
        {
            static_assert(sizeof...(Ts) == sizeof...(Us),
                          "send: argument count mismatch");

            value_type value{std::forward<Us>(vs)...};

            for (;;)
            {
                if (is_closed())
                    co_return false;

                if (queue_.try_enqueue(value))
                {
                    can_recv_.set();
                    extern AsyncEvent g_select_recv_event;
                    g_select_recv_event.set();
                    co_return true;
                }

                if (is_closed())
                    co_return false;

                co_await can_send_.wait();
            }
        }

        task::Awaitable<bool> send_tuple(value_type v)
        {
            for (;;)
            {
                if (is_closed())
                    co_return false;

                if (queue_.try_enqueue(v))
                {
                    can_recv_.set();
                    extern AsyncEvent g_select_recv_event;
                    g_select_recv_event.set();
                    co_return true;
                }

                if (is_closed())
                    co_return false;

                co_await can_send_.wait();
            }
        }

        task::Awaitable<std::optional<value_type>> recv()
        {
            value_type tmp;

            for (;;)
            {
                if (queue_.try_dequeue(tmp))
                {
                    can_send_.set();
                    co_return std::optional<value_type>{std::move(tmp)};
                }

                if (is_closed() && queue_.empty_relaxed())
                {
                    co_return std::nullopt;
                }

                co_await can_recv_.wait();
            }
        }

        /**
         * recv_into(out...)
         *  - кладет значения сразу по ссылкам в аргументы
         *  - false если канал закрыт и буфер пуст
         */
        template <class... Us>
        task::Awaitable<bool> recv_into(Us&... out)
        {
            static_assert(sizeof...(Ts) == sizeof...(Us),
                          "recv_into: argument count mismatch");

            value_type tmp;

            for (;;)
            {
                if (queue_.try_dequeue(tmp))
                {
                    assign_from_tuple(tmp, out...);
                    can_send_.set();
                    co_return true;
                }

                if (is_closed() && queue_.empty_relaxed())
                {
                    co_return false;
                }

                co_await can_recv_.wait();
            }
        }

    private:
        template <class Tup, class... Us, std::size_t... Is>
        static void assign_from_tuple_impl(Tup& t,
                                           std::tuple<Us&...> refs,
                                           std::index_sequence<Is...>)
        {
            ((std::get<Is>(refs) = std::get<Is>(t)), ...);
        }

        template <class... Us>
        static void assign_from_tuple(value_type& t, Us&... out)
        {
            std::tuple<Us&...> refs{out...};
            assign_from_tuple_impl(t, refs, std::index_sequence_for<Us...>{});
        }
    };

    template <class T>
    inline task::Awaitable<bool> operator<<(AsyncChannel<T>& ch, T v)
    {
        co_return co_await ch.send(std::move(v));
    }

    template <class... Ts, class... Us>
    inline task::Awaitable<bool> operator<<(AsyncChannel<Ts...>& ch, std::tuple<Us...> v)
    {
        static_assert(sizeof...(Ts) == sizeof...(Us),
                      "operator<<: tuple size mismatch");
        co_return co_await ch.send_tuple(std::move(v));
    }

    inline AsyncEvent g_select_recv_event{Reset::Auto, false};

    namespace detail
    {
        template <class C>
        using channel_value_t = typename C::value_type;

        template <class C0, class... Cs>
        constexpr bool all_same_value_type_v =
            (std::is_same_v<channel_value_t<C0>, channel_value_t<Cs>> && ...);
    }

    template <class Channel0, class... Channels>
    task::Awaitable<
        std::optional<std::pair<std::size_t, typename Channel0::value_type>>>
    select_recv(Channel0& c0, Channels&... cs)
    {
        static_assert(sizeof...(Channels) >= 1,
                      "select_recv: need at least 2 channels");
        static_assert(
            detail::all_same_value_type_v<Channel0, Channels...>,
            "select_recv: all channels must have the same value_type");

        using value_type = typename Channel0::value_type;
        using Result = std::optional<std::pair<std::size_t, value_type>>;

        Channel0* channels[] = {&c0, (&cs)...};
        constexpr std::size_t N = 1 + sizeof...(Channels);

        for (;;)
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                value_type v{};
                if (channels[i]->try_recv(v))
                {
                    co_return Result{std::make_pair(i, std::move(v))};
                }
            }

            bool any_open = false;
            for (std::size_t i = 0; i < N; ++i)
            {
                if (!channels[i]->is_closed() || !channels[i]->empty_relaxed())
                {
                    any_open = true;
                    break;
                }
            }
            if (!any_open)
            {
                co_return std::nullopt;
            }

            co_await g_select_recv_event.wait();
        }
    }

} // namespace usub::uvent::sync

#endif // UVENT_SYNC_ASYNC_CHANNEL_H
