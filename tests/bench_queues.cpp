#include <boost/lockfree/queue.hpp>
#include <chrono>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "uvent/utils/datastructures/queue/ConcurrentQueues.h"
#include "uvent/utils/datastructures/queue/IntrusiveMPSC.h"

using namespace usub::queue::concurrent;

namespace
{
    struct OldBounded
    {
        MPMCQueue<uint64_t> q{1024};
        void push(uint64_t v)
        {
            while (!q.try_enqueue(v))
                cpu_relax();
        }
        bool pop(uint64_t& v) { return q.try_dequeue(v); }
        size_t pop_bulk(uint64_t* out, size_t n) { return q.try_dequeue_bulk(out, n); }
    };

    struct OldBoundedBig
    {
        MPMCQueue<uint64_t> q{1u << 22};
        void push(uint64_t v)
        {
            while (!q.try_enqueue(v))
                cpu_relax();
        }
        bool pop(uint64_t& v) { return q.try_dequeue(v); }
        size_t pop_bulk(uint64_t* out, size_t n) { return q.try_dequeue_bulk(out, n); }
    };

    struct Segmented
    {
        SegmentedMPMCQueue<uint64_t> q;
        void push(uint64_t v) { q.enqueue(v); }
        bool pop(uint64_t& v) { return q.try_dequeue(v); }
        size_t pop_bulk(uint64_t* out, size_t n) { return q.try_dequeue_bulk(out, n); }
    };

    struct Boost
    {
        boost::lockfree::queue<uint64_t> q{1024};
        void push(uint64_t v)
        {
            while (!q.push(v))
                cpu_relax();
        }
        bool pop(uint64_t& v) { return q.pop(v); }
        size_t pop_bulk(uint64_t* out, size_t n)
        {
            size_t k = 0;
            while (k < n && q.pop(out[k]))
                ++k;
            return k;
        }
    };

    struct Node : MPSCNode
    {
        uint64_t v;
    };

    template <class Q>
    double run(unsigned producers, unsigned consumers, uint64_t total, bool bulk, const char* name)
    {
        Q q;
        std::atomic<bool> go{false};
        std::atomic<unsigned> done{0};
        std::atomic<uint64_t> consumed{0};
        const uint64_t per = total / producers;
        std::vector<std::thread> th;
        for (unsigned p = 0; p < producers; ++p)
            th.emplace_back([&] {
                while (!go.load(std::memory_order_acquire))
                    cpu_relax();
                for (uint64_t i = 0; i < per; ++i)
                    q.push(i);
                done.fetch_add(1, std::memory_order_release);
            });
        for (unsigned c = 0; c < consumers; ++c)
            th.emplace_back([&] {
                while (!go.load(std::memory_order_acquire))
                    cpu_relax();
                uint64_t v, n = 0;
                uint64_t buf[64];
                for (;;)
                {
                    size_t got = bulk ? q.pop_bulk(buf, 64) : (q.pop(v) ? 1 : 0);
                    if (got)
                    {
                        n += got;
                        continue;
                    }
                    if (done.load(std::memory_order_acquire) == producers)
                    {
                        got = bulk ? q.pop_bulk(buf, 64) : (q.pop(v) ? 1 : 0);
                        if (!got)
                            break;
                        n += got;
                    }
                    else
                        cpu_relax();
                }
                consumed.fetch_add(n);
            });
        auto t0 = std::chrono::steady_clock::now();
        go.store(true, std::memory_order_release);
        for (auto& t : th)
            t.join();
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (consumed.load() != per * producers)
        {
            std::fprintf(stderr, "LOST ITEMS in %s %ux%u bulk=%d: %llu vs %llu\n", name, producers, consumers, (int)bulk, (unsigned long long)consumed.load(),
                         (unsigned long long)(per * producers));
            std::abort();
        }
        return (per * producers) / sec / 1e6;
    }

    double run_mpsc_intrusive(unsigned producers, uint64_t total)
    {
        IntrusiveMPSCQueue<Node> q;
        const uint64_t per = total / producers;
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
                for (uint64_t i = 0; i < per; ++i)
                {
                    pools[p][i].v = i;
                    q.push(&pools[p][i]);
                }
                done.fetch_add(1, std::memory_order_release);
            });
        auto t0 = std::chrono::steady_clock::now();
        go.store(true, std::memory_order_release);
        uint64_t n = 0;
        for (;;)
        {
            Node* x = q.pop();
            if (x)
            {
                ++n;
                continue;
            }
            if (done.load(std::memory_order_acquire) == producers)
            {
                x = q.pop();
                if (!x)
                    break;
                ++n;
            }
            else
                cpu_relax();
        }
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        for (auto& t : th)
            t.join();
        if (n != per * producers)
            std::abort();
        return n / sec / 1e6;
    }
} // namespace

int main(int argc, char** argv)
{
    const uint64_t total = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 20'000'000;
    const unsigned hw = std::thread::hardware_concurrency();
    struct Cfg
    {
        unsigned p, c;
    };
    std::vector<Cfg> cfgs = {{1, 1}, {2, 2}, {4, 4}, {8, 8}};
    if (hw >= 24)
        cfgs.push_back({hw / 2, hw / 2});
    else if (hw >= 20)
        cfgs.push_back({10, 10});

    std::printf("items=%llu  (Mops/s, higher is better)\n", (unsigned long long)total);
    std::printf("%-8s %10s %10s %10s %10s | %10s\n", "PxC", "old1024", "old4M", "segmented", "boost", "seg-bulk");
    for (auto c : cfgs)
    {
        double a = run<OldBounded>(c.p, c.c, total, false, "old1024");
        double a2 = run<OldBoundedBig>(c.p, c.c, total, false, "old4M");
        double b = run<Segmented>(c.p, c.c, total, false, "segmented");
        double d = run<Boost>(c.p, c.c, total, false, "boost");
        double e = run<Segmented>(c.p, c.c, total, true, "seg-bulk");
        std::printf("%ux%-6u %10.2f %10.2f %10.2f %10.2f | %10.2f\n", c.p, c.c, a, a2, b, d, e);
    }

    std::printf("\nMPSC inbox pattern (Mops/s)\n");
    std::printf("%-8s %12s %12s %12s\n", "Px1", "intrusive", "segmented", "old1024");
    for (unsigned p : {1u, 2u, 4u, 8u})
    {
        double a = run_mpsc_intrusive(p, total);
        double b = run<Segmented>(p, 1, total, false, "segmented");
        double c = run<OldBounded>(p, 1, total, false, "old1024");
        std::printf("%ux1     %12.2f %12.2f %12.2f\n", p, a, b, c);
    }
    return 0;
}
