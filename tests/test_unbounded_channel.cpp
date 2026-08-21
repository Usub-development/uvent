#include <memory>

#include "test_common.h"
#include "uvent/Uvent.h"
#include "uvent/sync/AsyncUnboundedChannel.h"

using namespace usub::uvent;
using sync::AsyncUnboundedChannel;

namespace
{
    uint64_t scaled(uint64_t n)
    {
        if (const char* e = std::getenv("UVENT_TEST_SCALE"))
            if (auto d = std::strtoull(e, nullptr, 10); d > 1)
                return std::max<uint64_t>(1, n / d);
        return n;
    }

    void external_producers_coroutine_consumers()
    {
        constexpr int kThreads = 4, kProducers = 4, kConsumers = 4;
        const uint64_t kPer = scaled(100'000);
        const uint64_t kTotal = kProducers * kPer;

        AsyncUnboundedChannel<uint32_t, uint64_t> ch;
        std::atomic<uint64_t> got{0}, sum{0};
        std::atomic<int> consumers_done{0};

        usub::Uvent rt(kThreads);

        auto consumer = [&]() -> task::Awaitable<void> {
            for (;;)
            {
                auto r = co_await ch.recv();
                if (!r)
                    break;
                auto [p, seq] = *r;
                got.fetch_add(1, std::memory_order_relaxed);
                sum.fetch_add(seq, std::memory_order_relaxed);
            }
            if (consumers_done.fetch_add(1) + 1 == kConsumers)
                rt.stop();
        };
        for (int c = 0; c < kConsumers; ++c)
            system::co_spawn_static(consumer(), c % kThreads);

        std::vector<std::thread> producers;
        for (int p = 0; p < kProducers; ++p)
            producers.emplace_back([&, p] {
                for (uint64_t i = 0; i < kPer; ++i)
                    CHECK(ch.try_send(uint32_t(p), uint64_t(p) * kPer + i));
            });
        std::thread closer([&] {
            for (auto& t : producers)
                t.join();
            ch.close();
            CHECK(!ch.try_send(0u, 0ull));
        });

        rt.run();
        closer.join();

        CHECK_EQ(got.load(), kTotal);
        CHECK_EQ(sum.load(), kTotal * (kTotal - 1) / 2);
        CHECK(ch.empty_relaxed());
    }

    std::atomic<long> g_live{0};
    struct Tracked
    {
        std::unique_ptr<uint64_t> v;
        Tracked() { g_live.fetch_add(1); }
        explicit Tracked(uint64_t x) : v(std::make_unique<uint64_t>(x)) { g_live.fetch_add(1); }
        Tracked(Tracked&& o) noexcept : v(std::move(o.v)) { g_live.fetch_add(1); }
        Tracked& operator=(Tracked&& o) noexcept
        {
            v = std::move(o.v);
            return *this;
        }
        ~Tracked() { g_live.fetch_sub(1); }
    };

    void burst_then_drain_move_only()
    {
        const uint64_t kN = scaled(1'000'000);
        {
            AsyncUnboundedChannel<Tracked> ch;
            usub::Uvent rt(2);
            std::atomic<uint64_t> sum{0};

            auto producer = [&]() -> task::Awaitable<void> {
                for (uint64_t i = 0; i < kN; ++i)
                    CHECK(co_await ch.send(Tracked{i}));
                CHECK(ch.size_relaxed() <= kN);
                ch.close();
            };
            auto consumer = [&]() -> task::Awaitable<void> {
                Tracked t;
                uint64_t n = 0;
                while (co_await ch.recv_into(t))
                {
                    sum.fetch_add(*t.v, std::memory_order_relaxed);
                    ++n;
                }
                CHECK_EQ(n, kN);
                rt.stop();
            };
            system::co_spawn_static(producer(), 0);
            system::co_spawn_static(consumer(), 0);
            rt.run();
            CHECK_EQ(sum.load(), kN * (kN - 1) / 2);
        }
        CHECK_EQ(g_live.load(), 0L);
    }

    void select_over_two_channels()
    {
        const uint64_t kPer = scaled(50'000);
        AsyncUnboundedChannel<uint64_t> a, b;
        usub::Uvent rt(2);
        uint64_t from_a = 0, from_b = 0, sum = 0;

        auto prod = [&](AsyncUnboundedChannel<uint64_t>* ch, uint64_t base) -> task::Awaitable<void> {
            for (uint64_t i = 0; i < kPer; ++i)
            {
                co_await (*ch << (base + i));
                if ((i & 1023) == 0)
                    co_await system::this_coroutine::sleep_for(std::chrono::milliseconds(1));
            }
            ch->close();
        };
        auto cons = [&]() -> task::Awaitable<void> {
            for (;;)
            {
                auto r = co_await sync::select_recv(a, b);
                if (!r)
                    break;
                auto [idx, v] = *r;
                (idx == 0 ? from_a : from_b)++;
                sum += std::get<0>(v);
            }
            rt.stop();
        };
        system::co_spawn_static(prod(&a, 0), 0);
        system::co_spawn_static(prod(&b, kPer), 1);
        system::co_spawn_static(cons(), 0);
        rt.run();

        CHECK_EQ(from_a, kPer);
        CHECK_EQ(from_b, kPer);
        const uint64_t total = 2 * kPer;
        CHECK_EQ(sum, total * (total - 1) / 2);
    }
} // namespace

int main()
{
    return run_tests({
        {"external_producers_coroutine_consumers", external_producers_coroutine_consumers},
        {"burst_then_drain_move_only", burst_then_drain_move_only},
        {"select_over_two_channels", select_over_two_channels},
    });
}
