//
// Created by root on 10/21/25.
//

#ifndef TLS_UVENT_H
#define TLS_UVENT_H

#include <atomic>
#include <coroutine>
#include <uvent/base/Predefines.h>
#include <uvent/utils/datastructures/queue/FastQueue.h>
#include <uvent/utils/datastructures/queue/ConcurrentQueues.h>
#include <uvent/utils/intrinsincs/optimizations.h>

namespace usub::uvent::thread
{
    struct alignas(data_structures::metadata::CACHELINE_SIZE) ThreadLocalStorage
    {
        friend class system::Thread;

        void push_task_inbox(std::coroutine_handle<> task);

    private:
        queue::concurrent::MPMCQueue<std::coroutine_handle<>> inbox_q_;
        alignas(data_structures::metadata::CACHELINE_SIZE)
        std::atomic<uint32_t> inbox_seq_{0};
        uint32_t inbox_seq_seen_{0};
        char _pad_[data_structures::metadata::CACHELINE_SIZE
            - (sizeof(std::atomic<uint32_t>) + sizeof(uint32_t))]{};
    };
}

#endif //TLS_UVENT_H
