#include "test_common.h"
#include "uvent/Uvent.h"

using namespace usub::uvent;

namespace
{
    static_assert(is_thread_affine_v<net::TCPClientSocket>);
    static_assert(is_thread_affine_v<net::TCPServerSocket>);
    static_assert(is_thread_affine_v<utils::Timer>);
    static_assert(!is_thread_affine_v<int>);
    static_assert(!is_thread_affine_v<std::string>);

    static_assert(system::SharedSpawnable<task::Awaitable<void>>);
    static_assert(system::SharedSpawnable<task::Awaitable<int>>);
    static_assert(!system::SharedSpawnable<task::LocalAwaitable<void>>);
    static_assert(!system::SharedSpawnable<task::LocalAwaitable<int>>);
    static_assert(!system::SharedSpawnable<task::Awaitable<ssize_t, detail::AwaitableIOFrame<ssize_t>>>);

    template <class F>
    concept CoSpawnable = requires(F f) { system::co_spawn(std::forward<F>(f)); };

    static_assert(CoSpawnable<task::Awaitable<void>>);
    static_assert(!CoSpawnable<task::LocalAwaitable<void>>);

    std::atomic<bool> g_local_ran{false};

    task::LocalAwaitable<void> local_pinned_body(usub::Uvent* rt)
    {
        co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(1));
#ifdef UVENT_ENABLE_REUSEADDR
        CHECK_EQ(system::this_thread::detail::t_id, 1);
#endif
        g_local_ran.store(true, std::memory_order_release);
        rt->stop();
    }

    void local_awaitable_runs_pinned()
    {
        usub::Uvent rt(2);
        system::co_spawn_static(local_pinned_body(&rt), 1);
        rt.run();
        CHECK(g_local_ran.load());
    }

    task::Awaitable<int> inner_local_child() { co_return 5; }

    task::LocalAwaitable<int> local_with_child()
    {
        const int v = co_await inner_local_child();
        co_return v + 1;
    }

    std::atomic<int> g_nested{0};

    task::LocalAwaitable<void> local_nested_body(usub::Uvent* rt)
    {
        g_nested.store(co_await local_with_child(), std::memory_order_release);
        rt->stop();
    }

    void local_awaitable_composes()
    {
        usub::Uvent rt(2);
        system::co_spawn_static(local_nested_body(&rt), 0);
        rt.run();
        CHECK_EQ(g_nested.load(), 6);
    }
} // namespace

int main()
{
    return run_tests({
        {"local_awaitable_runs_pinned", local_awaitable_runs_pinned},
        {"local_awaitable_composes", local_awaitable_composes},
    });
}
