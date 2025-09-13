//
// Created by root on 9/11/25.
//

#ifndef QSBR_H
#define QSBR_H

#include <atomic>
#include <limits>
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
        };

        struct Retired
        {
            void (*deleter)(void*);
            void* p;
            uint64_t epoch;
        };

        void attach_current_thread();
        void detach_current_thread();

        void enter() noexcept;
        void leave() noexcept;

        void retire(void* p, void (*deleter)(void*)) noexcept;

        void quiesce_tick();

    private:
        std::atomic<uint64_t> global_epoch_{1};
        std::mutex reg_mu_;
        std::vector<ThreadState*> threads_;

        static thread_local ThreadState* tls_;
        static thread_local std::vector<Retired> retired_tls_;

        uint64_t min_active_epoch() const noexcept;
    };
}

#endif //QSBR_H
