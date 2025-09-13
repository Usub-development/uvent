//
// Created by kirill on 11/17/24.
//

#ifndef UVENT_SHAREDTASKS_H
#define UVENT_SHAREDTASKS_H

#include <mutex>
#include <memory>
#include "Awaitable.h"
#include "include/uvent/utils/datastructures/queue/ConcurrentQueues.h"

namespace usub::uvent::task {
    class SharedTasks {
    public:
        SharedTasks();

        ~SharedTasks() = default;

        void enqueue(std::coroutine_handle<> &task);

        void enqueue(std::coroutine_handle<> &&task);

        bool dequeue(std::coroutine_handle<> &task);

        bool dequeue(std::coroutine_handle<> &&task);

        std::size_t getSize();

    private:
        std::unique_ptr<queue::concurrent::MPMCQueue<std::coroutine_handle<>>> detachedTasks;
    };
}

#endif //UVENT_SHAREDTASKS_H
