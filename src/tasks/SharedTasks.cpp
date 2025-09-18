//
// Created by kirill on 11/17/24.
//

#include "uvent/tasks/SharedTasks.h"

namespace usub::uvent::task {

    SharedTasks::SharedTasks()
    {
        this->detachedTasks = std::make_unique<queue::concurrent::MPMCQueue<std::coroutine_handle<>>>();
    }

    void SharedTasks::enqueue(std::coroutine_handle<> &task) {
        this->detachedTasks->try_enqueue(task);
    }

    bool SharedTasks::dequeue(std::coroutine_handle<> &task) {
        return this->detachedTasks->try_dequeue(task);
    }

    std::size_t SharedTasks::getSize() {
        return this->detachedTasks->size();
    }

    void SharedTasks::enqueue(std::coroutine_handle<> &&task) {
        this->detachedTasks->try_enqueue(task);
    }

    bool SharedTasks::dequeue(std::coroutine_handle<> &&task) {
        return this->detachedTasks->try_dequeue(task);
    }
}
