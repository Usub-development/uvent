#include "uvent/system/Introspection.h"

#ifdef UVENT_TASK_INTROSPECTION

#include <atomic>
#include <chrono>
#include <inttypes.h>

#include "uvent/tasks/AwaitableFrame.h"
#include "uvent/utils/intrinsics/optimizations.h"

namespace usub::uvent::introspection
{
    namespace detail
    {
        struct Shard
        {
            std::atomic_flag lk = ATOMIC_FLAG_INIT;
            uvent::detail::AwaitableFrameBase* head{nullptr};

            void lock() noexcept
            {
                while (this->lk.test_and_set(std::memory_order_acquire))
                    cpu_relax();
            }

            void unlock() noexcept { this->lk.clear(std::memory_order_release); }
        };

        constexpr std::size_t kShards = 64;

        inline Shard g_shards[kShards];

        uint64_t now_ns() noexcept
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }

        inline Shard& shard_for_current() noexcept
        {
            static std::atomic<uint32_t> counter{0};
            thread_local uint32_t idx = counter.fetch_add(1, std::memory_order_relaxed) % kShards;
            return g_shards[idx];
        }

        void register_frame(uvent::detail::AwaitableFrameBase* f) noexcept
        {
            auto& sh = shard_for_current();
            f->created_ns_ = now_ns();
            f->reg_shard_ = &sh;
            sh.lock();
            f->reg_prev_ = nullptr;
            f->reg_next_ = sh.head;
            if (sh.head)
                sh.head->reg_prev_ = f;
            sh.head = f;
            sh.unlock();
        }

        void unregister_frame(uvent::detail::AwaitableFrameBase* f) noexcept
        {
            auto* sh = f->reg_shard_;
            if (!sh)
                return;
            sh->lock();
            if (f->reg_prev_)
                f->reg_prev_->reg_next_ = f->reg_next_;
            else
                sh->head = f->reg_next_;
            if (f->reg_next_)
                f->reg_next_->reg_prev_ = f->reg_prev_;
            sh->unlock();
            f->reg_shard_ = nullptr;
        }
    } // namespace detail

    std::vector<TaskInfo> snapshot()
    {
        std::vector<TaskInfo> out;
        out.reserve(256);
        const uint64_t now = detail::now_ns();
        for (auto& sh : detail::g_shards)
        {
            sh.lock();
            for (auto* f = sh.head; f; f = f->reg_next_)
            {
                TaskInfo info;
                info.frame = f->get_coroutine_handle().address();
                auto prev = f->prev_handle();
                info.parent = prev ? prev.address() : nullptr;
                info.name = f->name();
                info.wait_reason = f->wait_reason();
                info.trace_id = f->trace_id();
                info.thread_id = f->get_thread_id();
                info.cancel_requested = f->cancel_requested();
                info.has_cancel_hook = f->has_cancel_hook();
                info.age_ns = now - f->created_ns();
                info.waiting_ns = f->wait_since_ns() ? now - f->wait_since_ns() : 0;
                out.push_back(info);
            }
            sh.unlock();
        }
        return out;
    }

    std::size_t live_count() noexcept
    {
        std::size_t n = 0;
        for (auto& sh : detail::g_shards)
        {
            sh.lock();
            for (auto* f = sh.head; f; f = f->reg_next_)
                ++n;
            sh.unlock();
        }
        return n;
    }

    void dump(std::FILE* out)
    {
        auto* o = out ? out : stderr;
        auto tasks = snapshot();
        std::fprintf(o, "uvent tasks: %zu live\n", tasks.size());
        for (const auto& t : tasks)
        {
            std::fprintf(o,
                         "  frame=%p thread=%d name=%s wait=%s trace=%" PRIu64 " age_ms=%" PRIu64
                         " waiting_ms=%" PRIu64 "%s%s\n",
                         t.frame, t.thread_id, t.name ? t.name : "-", t.wait_reason ? t.wait_reason : "-",
                         t.trace_id, t.age_ns / 1000000, t.waiting_ns / 1000000,
                         t.cancel_requested ? " cancel-requested" : "", t.has_cancel_hook ? " cancellable-wait" : "");
        }
    }
} // namespace usub::uvent::introspection

#endif
