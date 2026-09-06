#ifndef QSBR_H
#define QSBR_H

#include <atomic>
#include <limits>
#include <mutex>
#include <vector>

namespace usub::utils::sync
{
    class QSBR
    {
    public:
        struct ThreadState
        {
            std::atomic<uint64_t> epoch{0};
            std::atomic<bool> active{false};
            std::atomic<bool> in_use{false};
            ThreadState* next{nullptr};
        };

        struct Retired
        {
            void (*deleter)(void*);
            void* p;
            uint64_t epoch;
        };

        ~QSBR();

        void attach_current_thread();
        void detach_current_thread();

        void enter() noexcept;
        void leave() noexcept;

        void retire(void* p, void (*deleter)(void*)) noexcept;

        void quiesce_tick();

    private:
        std::atomic<ThreadState*> head_{nullptr};
        std::atomic<uint64_t> global_epoch_{1};
        std::mutex orphan_mu_;
        std::vector<Retired> orphans_;
        std::atomic<size_t> orphan_count_{0};

        static thread_local ThreadState* tls_;
        static thread_local std::vector<Retired> retired_tls_;

        uint64_t min_active_epoch() const noexcept;

        void adopt_orphans_into(std::vector<Retired>& out, uint64_t safe);
    };
} // namespace usub::utils::sync

#endif // QSBR_H
