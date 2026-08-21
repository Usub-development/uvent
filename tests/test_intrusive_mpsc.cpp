#include <memory>
#include "test_common.h"
#include "uvent/utils/datastructures/queue/IntrusiveMPSC.h"

using namespace usub::queue::concurrent;

namespace
{
    struct Node : MPSCNode
    {
        uint32_t producer{0};
        uint64_t seq{0};
    };

    void fifo_single()
    {
        IntrusiveMPSCQueue<Node> q;
        CHECK(q.pop() == nullptr);
        std::unique_ptr<Node[]> nodes(new Node[1000]);
        const size_t nodes_n = 1000;
        for (size_t i = 0; i < nodes_n; ++i)
        {
            nodes[i].seq = i;
            q.push(&nodes[i]);
        }
        for (size_t i = 0; i < nodes_n; ++i)
        {
            Node* n = q.pop();
            CHECK(n != nullptr);
            CHECK_EQ(n->seq, i);
        }
        CHECK(q.pop() == nullptr);
        nodes[0].seq = 7777;
        q.push(&nodes[0]);
        Node* n = q.pop();
        CHECK(n && n->seq == 7777);
        CHECK(q.pop() == nullptr);
    }

    void mpsc_stress()
    {
        IntrusiveMPSCQueue<Node> q;
        const unsigned producers = std::max(2u, std::min(8u, hw_threads() - 1));
        const uint64_t per = 500'000;
        std::vector<std::unique_ptr<Node[]>> pools;
        for (unsigned p = 0; p < producers; ++p)
            pools.emplace_back(new Node[per]);
        std::atomic<bool> go{false};
        std::atomic<unsigned> done{0};

        std::vector<std::thread> th;
        for (unsigned p = 0; p < producers; ++p)
            th.emplace_back([&, p] {
                while (!go.load(std::memory_order_acquire))
                    cpu_relax();
                for (uint64_t s = 0; s < per; ++s)
                {
                    pools[p][s].producer = p;
                    pools[p][s].seq = s;
                    q.push(&pools[p][s]);
                }
                done.fetch_add(1, std::memory_order_release);
            });

        std::vector<uint64_t> next(producers, 0);
        uint64_t total = 0;
        go.store(true, std::memory_order_release);
        for (;;)
        {
            Node* n = q.pop();
            if (!n)
            {
                if (done.load(std::memory_order_acquire) == producers)
                {
                    n = q.pop();
                    if (!n)
                        break;
                }
                else
                {
                    cpu_relax();
                    continue;
                }
            }
            CHECK_EQ(n->seq, next[n->producer]);
            next[n->producer]++;
            ++total;
        }
        for (auto& t : th)
            t.join();
        CHECK_EQ(total, uint64_t(producers) * per);
    }

    void reuse_nodes_many_rounds()
    {
        IntrusiveMPSCQueue<Node> q;
        std::unique_ptr<Node[]> nodes(new Node[64]);
        for (int round = 0; round < 100000; ++round)
        {
            for (size_t i = 0; i < 64; ++i)
                q.push(&nodes[i]);
            for (size_t i = 0; i < 64; ++i)
                CHECK(q.pop() == &nodes[i]);
            CHECK(q.pop() == nullptr);
        }
    }
} // namespace

int main()
{
    return run_tests({
        {"fifo_single", fifo_single},
        {"reuse_nodes_many_rounds", reuse_nodes_many_rounds},
        {"mpsc_stress", mpsc_stress},
    });
}
