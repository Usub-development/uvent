#include "uvent/net/HappyEyeballs.h"

#include "uvent/system/SystemContext.h"

#include <mutex>
#include <optional>

namespace usub::uvent::net
{
    namespace
    {
        using ConnectError = usub::utils::errors::ConnectError;

        struct HEState
        {
            std::mutex mtx;
            bool done{false};
            bool parent_waiting{false};
            std::coroutine_handle<> parent;
            int parent_tid{0};

            std::vector<ResolvedAddr> addrs;
            std::string port;
            std::chrono::milliseconds attempt_timeout{};
            size_t max_attempts{0};

            size_t next_idx{0};
            size_t finished{0};
            int resolves_pending{0};
            bool v6_resolved{false};

            std::optional<TCPClientSocket> winner;
            std::optional<ConnectError> last_error;

            bool decided() const
            {
                return this->done ||
                       (this->resolves_pending == 0 && this->next_idx == this->addrs.size() &&
                        this->finished == this->addrs.size());
            }
        };

        void resume_parent(std::coroutine_handle<> h, [[maybe_unused]] int tid)
        {
#ifdef UVENT_ENABLE_REUSEADDR
            system::this_thread::detail::q.enqueue(h);
#else
            system::this_thread::detail::st->enqueue(h);
#endif
        }

        std::coroutine_handle<> take_parent_if_decided(HEState& st)
        {
            if (st.decided() && st.parent_waiting)
            {
                st.parent_waiting = false;
                return std::exchange(st.parent, {});
            }
            return {};
        }

        struct RaceAwaiter
        {
            std::shared_ptr<HEState> st;

            bool await_ready() const
            {
                std::lock_guard lk(this->st->mtx);
                return this->st->decided();
            }

            bool await_suspend(std::coroutine_handle<> h) const
            {
                std::lock_guard lk(this->st->mtx);
                if (this->st->decided())
                    return false;
                this->st->parent = h;
                this->st->parent_waiting = true;
                return true;
            }

            void await_resume() const {}
        };

        task::Awaitable<void> he_attempt(std::shared_ptr<HEState> st, ResolvedAddr addr);

        void spawn_on_race_thread(const std::shared_ptr<HEState>& st, task::Awaitable<void>&& coro)
        {
#ifdef UVENT_ENABLE_REUSEADDR
            system::co_spawn_static(std::move(coro), st->parent_tid);
#else
            system::co_spawn(std::move(coro));
#endif
        }

        void launch_next(const std::shared_ptr<HEState>& st)
        {
            ResolvedAddr addr;
            {
                std::lock_guard lk(st->mtx);
                if (st->done || st->next_idx >= st->addrs.size())
                    return;
                addr = st->addrs[st->next_idx++];
            }
            spawn_on_race_thread(st, he_attempt(st, std::move(addr)));
        }

        task::Awaitable<void> he_attempt(std::shared_ptr<HEState> st, ResolvedAddr addr)
        {
            {
                bool skip = false;
                std::coroutine_handle<> to_wake{};
                {
                    std::lock_guard lk(st->mtx);
                    if (st->done)
                    {
                        skip = true;
                        ++st->finished;
                        to_wake = take_parent_if_decided(*st);
                    }
                }
                if (to_wake)
                    resume_parent(to_wake, st->parent_tid);
                if (skip)
                    co_return;
            }

            TCPClientSocket sock;
            sock.ipv = addr.ipv;
            std::string port = st->port;
            auto err = co_await sock.async_connect(std::move(addr.ip), std::move(port), st->attempt_timeout);

            std::coroutine_handle<> to_wake{};
            bool lost_after_success = false;
            bool launch_early = false;
            {
                std::lock_guard lk(st->mtx);
                ++st->finished;
                if (!err.has_value())
                {
                    if (!st->done)
                    {
                        st->done = true;
                        st->winner.emplace(std::move(sock));
                    }
                    else
                        lost_after_success = true;
                }
                else
                {
                    st->last_error = *err;
                    launch_early = !st->done && st->next_idx < st->addrs.size();
                }
                to_wake = take_parent_if_decided(*st);
            }
            if (lost_after_success)
                sock.shutdown();
            if (launch_early)
                launch_next(st);
            if (to_wake)
                resume_parent(to_wake, st->parent_tid);
            co_return;
        }

        task::Awaitable<void> he_pacer(std::shared_ptr<HEState> st, std::chrono::milliseconds delay)
        {
            for (;;)
            {
                co_await system::this_coroutine::sleep_for(delay);
                {
                    std::lock_guard lk(st->mtx);
                    if (st->done || (st->resolves_pending == 0 && st->next_idx >= st->addrs.size()))
                        co_return;
                }
                launch_next(st);
            }
        }

        void kick_if_stalled(const std::shared_ptr<HEState>& st)
        {
            {
                std::lock_guard lk(st->mtx);
                if (st->done || st->next_idx >= st->addrs.size() || st->finished != st->next_idx)
                    return;
            }
            launch_next(st);
        }

        void collect_numeric(const addrinfo* ai_list, std::vector<ResolvedAddr>& out)
        {
            for (const addrinfo* ai = ai_list; ai != nullptr; ai = ai->ai_next)
            {
                char buf[NI_MAXHOST];
                if (::getnameinfo(ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen), buf, sizeof(buf), nullptr, 0,
                                  NI_NUMERICHOST) != 0)
                    continue;
                if (ai->ai_family == AF_INET6)
                    out.push_back({buf, utils::net::IPV6});
                else if (ai->ai_family == AF_INET)
                    out.push_back({buf, utils::net::IPV4});
            }
        }

        void weave_tail(HEState& st, std::vector<ResolvedAddr>&& found, bool found_first)
        {
            std::vector<ResolvedAddr> tail(std::make_move_iterator(st.addrs.begin() + st.next_idx),
                                           std::make_move_iterator(st.addrs.end()));
            st.addrs.resize(st.next_idx);

            const size_t cap = st.max_attempts ? std::max(st.max_attempts, st.next_idx) : SIZE_MAX;
            auto& first = found_first ? found : tail;
            auto& second = found_first ? tail : found;
            for (size_t i = 0; i < first.size() || i < second.size(); ++i)
            {
                if (i < first.size() && st.addrs.size() < cap)
                    st.addrs.push_back(std::move(first[i]));
                if (i < second.size() && st.addrs.size() < cap)
                    st.addrs.push_back(std::move(second[i]));
            }
        }

        task::Awaitable<void> he_resolve(std::shared_ptr<HEState> st, std::string host, std::string service, bool v6,
                                         std::chrono::milliseconds resolution_delay)
        {
            addrinfo hints{};
            hints.ai_family = v6 ? AF_INET6 : AF_INET;
            hints.ai_socktype = SOCK_STREAM;
#ifdef AI_ADDRCONFIG
            hints.ai_flags = AI_ADDRCONFIG;
#endif
            auto resolved = co_await async_resolve(std::move(host), std::move(service), hints);

            std::vector<ResolvedAddr> found;
            if (resolved && *resolved)
                collect_numeric(resolved->get(), found);

            if (!v6 && !found.empty() && resolution_delay.count() > 0)
            {
                bool aaaa_pending;
                {
                    std::lock_guard lk(st->mtx);
                    aaaa_pending = !st->v6_resolved && !st->done;
                }
                if (aaaa_pending)
                    co_await system::this_coroutine::sleep_for(resolution_delay);
            }

            std::coroutine_handle<> to_wake{};
            {
                std::lock_guard lk(st->mtx);
                if (v6)
                    st->v6_resolved = true;
                --st->resolves_pending;
                if (!st->done && !found.empty())
                    weave_tail(*st, std::move(found), /*found_first=*/v6);
                to_wake = take_parent_if_decided(*st);
            }
            kick_if_stalled(st);
            if (to_wake)
                resume_parent(to_wake, st->parent_tid);
            co_return;
        }
    } // namespace

    task::Awaitable<std::expected<TCPClientSocket, ConnectError>>
    connect_happy_addrs(std::vector<ResolvedAddr> addrs, std::string port, HappyEyeballsOptions opts)
    {
        if (addrs.empty())
            co_return std::unexpected(ConnectError::GetAddrInfoFailed);
        if (opts.max_attempts > 0 && addrs.size() > opts.max_attempts)
            addrs.resize(opts.max_attempts);

        auto st = std::make_shared<HEState>();
        st->addrs = std::move(addrs);
        st->port = std::move(port);
        st->attempt_timeout = opts.attempt_timeout;
        st->max_attempts = opts.max_attempts;
        st->parent_tid = system::this_thread::detail::t_id;

        const bool need_pacer = st->addrs.size() > 1;
        launch_next(st); // первый адрес — сразу, без стаггера
        if (need_pacer)
            spawn_on_race_thread(st, he_pacer(st, opts.attempt_delay));

        co_await RaceAwaiter{st};

        std::lock_guard lk(st->mtx);
        if (st->winner)
            co_return std::move(*st->winner);
        co_return std::unexpected(st->last_error.value_or(ConnectError::ConnectFailed));
    }

    task::Awaitable<std::expected<TCPClientSocket, ConnectError>>
    connect_happy(std::string host, std::string port, HappyEyeballsOptions opts)
    {
        auto st = std::make_shared<HEState>();
        st->port = port;
        st->attempt_timeout = opts.attempt_timeout;
        st->max_attempts = opts.max_attempts;
        st->parent_tid = system::this_thread::detail::t_id;
        st->resolves_pending = 2;

        spawn_on_race_thread(st, he_resolve(st, host, port, /*v6=*/true, opts.resolution_delay));
        spawn_on_race_thread(st, he_resolve(st, std::move(host), std::move(port), /*v6=*/false, opts.resolution_delay));
        spawn_on_race_thread(st, he_pacer(st, opts.attempt_delay));

        co_await RaceAwaiter{st};

        std::lock_guard lk(st->mtx);
        if (st->winner)
            co_return std::move(*st->winner);
        if (st->addrs.empty())
            co_return std::unexpected(ConnectError::GetAddrInfoFailed);
        co_return std::unexpected(st->last_error.value_or(ConnectError::ConnectFailed));
    }
} // namespace usub::uvent::net
