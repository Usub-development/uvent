//
// Created by root on 10/21/25.
//

#ifndef TLS_UVENT_H
#define TLS_UVENT_H

#include <atomic>
#include <coroutine>
#include <uvent/base/Predefines.h>
#include <uvent/utils/datastructures/queue/ConcurrentQueues.h>
#include <uvent/utils/datastructures/queue/FastQueue.h>

namespace usub::uvent::thread
{
    struct alignas(data_structures::metadata::CACHELINE_SIZE) ThreadLocalStorage
    {
        friend class system::Thread;

        void push_task_inbox(std::coroutine_handle<> task);

    private:
        queue::concurrent::MPMCQueue<std::coroutine_handle<>> inbox_q_;
        std::atomic_bool is_added_new_{false};
    };
} // namespace usub::uvent::thread

#endif // TLS_UVENT_H
