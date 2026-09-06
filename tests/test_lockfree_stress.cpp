#include <memory>

#include "test_common.h"
#include "uvent/utils/sync/HazardPointers.h"
#include "uvent/utils/sync/QSBR.h"

using usub::utils::sync::HazardDomain;
using usub::utils::sync::QSBR;

namespace
{
    uint64_t scaled(uint64_t n)
    {
        if (const char* e = std::getenv("UVENT_TEST_SCALE"))
            if (auto d = std::strtoull(e, nullptr, 10); d > 1)
                return std::max<uint64_t>(1, n / d);
        return n;
    }

    struct Node
    {
        std::atomic<Node*> next{nullptr};
        uint64_t payload{0};
    };

    std::atomic<uint64_t> g_deleted{0};

    void delete_node(void* p)
    {
        delete static_cast<Node*>(p);
        g_deleted.fetch_add(1, std::memory_order_relaxed);
    }

    void hazard_pointer_stack_stress()
    {
        g_deleted.store(0);
        auto& dom = HazardDomain::instance();
        std::atomic<Node*> head{nullptr};
        const unsigned threads = std::min(8u, hw_threads());
        const uint64_t per_thread = scaled(20'000);
        std::atomic<uint64_t> popped{0};
        std::atomic<uint64_t> pushed{0};

        std::vector<std::thread> ts;
        for (unsigned t = 0; t < threads; ++t)
        {
            ts.emplace_back([&, t] {
                auto* rec = dom.local_record();
                for (uint64_t i = 0; i < per_thread; ++i)
                {
                    if ((i + t) % 2 == 0)
                    {
                        auto* n = new Node{};
                        n->payload = i;
                        Node* h = head.load(std::memory_order_relaxed);
                        do
                        {
                            n->next.store(h, std::memory_order_relaxed);
                        } while (!head.compare_exchange_weak(h, n, std::memory_order_release,
                                                             std::memory_order_relaxed));
                        pushed.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        for (;;)
                        {
                            Node* h = dom.protect(rec, head);
                            if (!h)
                                break;
                            Node* next = h->next.load(std::memory_order_acquire);
                            Node* expected = h;
                            if (head.compare_exchange_strong(expected, next, std::memory_order_acq_rel,
                                                             std::memory_order_relaxed))
                            {
                                dom.retire(rec, h, &delete_node);
                                popped.fetch_add(1, std::memory_order_relaxed);
                                break;
                            }
                        }
                    }
                }
            });
        }
        for (auto& t : ts)
            t.join();

        uint64_t drained = 0;
        {
            auto* rec = dom.local_record();
            Node* h = head.exchange(nullptr, std::memory_order_acq_rel);
            while (h)
            {
                Node* next = h->next.load(std::memory_order_relaxed);
                dom.retire(rec, h, &delete_node);
                ++drained;
                h = next;
            }
            dom.scan(rec);
        }
        CHECK_EQ(popped.load() + drained, pushed.load());
        CHECK(g_deleted.load() <= pushed.load());
    }

    std::atomic<uint64_t> g_qsbr_deleted{0};

    void delete_u64(void* p)
    {
        delete static_cast<uint64_t*>(p);
        g_qsbr_deleted.fetch_add(1, std::memory_order_relaxed);
    }

    void qsbr_retire_stress()
    {
        g_qsbr_deleted.store(0);
        QSBR qsbr;
        const unsigned threads = std::min(8u, hw_threads());
        const uint64_t per_thread = scaled(50'000);
        std::atomic<uint64_t> retired{0};

        std::vector<std::thread> ts;
        for (unsigned t = 0; t < threads; ++t)
        {
            ts.emplace_back([&] {
                qsbr.attach_current_thread();
                for (uint64_t i = 0; i < per_thread; ++i)
                {
                    qsbr.enter();
                    auto* v = new uint64_t(i);
                    qsbr.leave();
                    qsbr.retire(v, &delete_u64);
                    retired.fetch_add(1, std::memory_order_relaxed);
                    if (i % 64 == 0)
                        qsbr.quiesce_tick();
                }
                for (int i = 0; i < 64; ++i)
                    qsbr.quiesce_tick();
                qsbr.detach_current_thread();
            });
        }
        for (auto& t : ts)
            t.join();
        CHECK_EQ(retired.load(), uint64_t(threads) * per_thread);
        CHECK(g_qsbr_deleted.load() <= retired.load());
        CHECK(g_qsbr_deleted.load() > 0);
    }
} // namespace

int main()
{
    return run_tests({
        {"hazard_pointer_stack_stress", hazard_pointer_stack_stress},
        {"qsbr_retire_stress", qsbr_retire_stress},
    });
}
