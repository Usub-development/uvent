#include "test_common.h"
#include "uvent/Uvent.h"
#include "uvent/sync/AsyncUnboundedChannel.h"

using namespace usub::uvent;
using namespace std::chrono_literals;

namespace
{
    void select_timeout_wins()
    {
        usub::Uvent rt(2);
        std::atomic<int> branch{-100};

        auto body = [&]() -> task::Awaitable<void> {
            sync::AsyncUnboundedChannel<int> ch;
            auto r = co_await sync::select(ch.recv_op(), sync::sleep_op(50ms));
            branch.store(r.index, std::memory_order_release);
            rt.stop();
        };
        system::co_spawn_static(body(), 0);
        auto t0 = std::chrono::steady_clock::now();
        rt.run();
        CHECK_EQ(branch.load(), 1);
        CHECK(std::chrono::steady_clock::now() - t0 < 5s);
    }

    void select_ready_channel_wins()
    {
        usub::Uvent rt(2);
        std::atomic<int> got{0};

        auto body = [&]() -> task::Awaitable<void> {
            sync::AsyncUnboundedChannel<int> ch;
            CHECK(ch.try_send(7));
            auto r = co_await sync::select(ch.recv_op(), sync::sleep_op(10s));
            CHECK_EQ(r.index, 0);
            auto& v = r.get<0>();
            CHECK(v.has_value());
            got.store(std::get<0>(*v), std::memory_order_release);
            rt.stop();
        };
        system::co_spawn_static(body(), 0);
        rt.run();
        CHECK_EQ(got.load(), 7);
    }

    void select_cross_thread_send()
    {
        usub::Uvent rt(2);
        sync::AsyncUnboundedChannel<uint64_t> ch;
        std::atomic<uint64_t> got{0};

        auto body = [&]() -> task::Awaitable<void> {
            auto r = co_await sync::select(ch.recv_op(), sync::sleep_op(10s));
            CHECK_EQ(r.index, 0);
            auto& v = r.get<0>();
            CHECK(v.has_value());
            got.store(std::get<0>(*v), std::memory_order_release);
            rt.stop();
        };
        system::co_spawn_static(body(), 0);
        std::thread sender([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            CHECK(ch.try_send(uint64_t{99}));
        });
        rt.run();
        sender.join();
        CHECK_EQ(got.load(), 99u);
    }

    void select_cancel_token_branch()
    {
        usub::Uvent rt(2);
        sync::CancellationSource src;
        std::atomic<int> branch{-100};

        auto body = [&]() -> task::Awaitable<void> {
            sync::AsyncUnboundedChannel<int> ch;
            auto r = co_await sync::select(ch.recv_op(), src.token().on_cancel_op(), sync::sleep_op(10s));
            branch.store(r.index, std::memory_order_release);
            rt.stop();
        };
        system::co_spawn_static(body(), 0);
        std::thread killer([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            src.request_cancel();
        });
        auto t0 = std::chrono::steady_clock::now();
        rt.run();
        killer.join();
        CHECK_EQ(branch.load(), 1);
        CHECK(std::chrono::steady_clock::now() - t0 < 5s);
    }

    void select_own_task_cancelled()
    {
        usub::Uvent rt(2);
        std::atomic<bool> was_cancelled{false};

        auto selecting = [&]() -> task::Awaitable<void> {
            sync::AsyncUnboundedChannel<int> ch;
            auto r = co_await sync::select(ch.recv_op(), sync::sleep_op(60s));
            was_cancelled.store(r.cancelled(), std::memory_order_release);
        };
        auto driver = [&]() -> task::Awaitable<void> {
            auto h = task::spawn(selecting());
            co_await system::this_coroutine::sleep_for(50ms);
            h.cancel();
            co_await h;
            rt.stop();
        };
        system::co_spawn_static(driver(), 0);
        auto t0 = std::chrono::steady_clock::now();
        rt.run();
        CHECK(was_cancelled.load());
        CHECK(std::chrono::steady_clock::now() - t0 < 5s);
    }

    void select_loop_timer_ownership()
    {
        usub::Uvent rt(2);
        std::atomic<uint64_t> got{0};

        auto consumer = [&]() -> task::Awaitable<void> {
            sync::AsyncUnboundedChannel<uint64_t> ch;
            for (uint64_t i = 1; i <= 500; ++i)
                CHECK(ch.try_send(i));
            ch.close();
            uint64_t sum = 0;
            for (;;)
            {
                auto r = co_await sync::select(ch.recv_op(), sync::sleep_op(200ms));
                if (r.is<1>())
                    continue;
                auto& v = r.get<0>();
                if (!v.has_value())
                    break;
                sum += std::get<0>(*v);
            }
            got.store(sum, std::memory_order_release);
            rt.stop();
        };
        system::co_spawn_static(consumer(), 0);
        rt.run();
        CHECK_EQ(got.load(), 500ull * 501 / 2);
    }

    void legacy_select_recv()
    {
        usub::Uvent rt(2);
        std::atomic<bool> ok{false};

        auto body = [&]() -> task::Awaitable<void> {
            sync::AsyncUnboundedChannel<int> a;
            sync::AsyncUnboundedChannel<int> b;
            CHECK(b.try_send(5));
            auto r = co_await sync::select_recv(a, b);
            CHECK(r.has_value());
            CHECK_EQ(r->first, 1u);
            CHECK_EQ(std::get<0>(r->second), 5);

            a.close();
            b.close();
            auto r2 = co_await sync::select_recv(a, b);
            CHECK(!r2.has_value());
            ok.store(true, std::memory_order_release);
            rt.stop();
        };
        system::co_spawn_static(body(), 0);
        rt.run();
        CHECK(ok.load());
    }
} // namespace

int main()
{
    return run_tests({
        {"select_timeout_wins", select_timeout_wins},
        {"select_ready_channel_wins", select_ready_channel_wins},
        {"select_cross_thread_send", select_cross_thread_send},
        {"select_cancel_token_branch", select_cancel_token_branch},
#ifdef UVENT_ENABLE_REUSEADDR
        {"select_own_task_cancelled", select_own_task_cancelled},
#endif
        {"select_loop_timer_ownership", select_loop_timer_ownership},
        {"legacy_select_recv", legacy_select_recv},
    });
}
