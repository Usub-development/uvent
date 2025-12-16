//
// Created by root on 10/21/25.
//

#include <uvent/pool/TLS.h>

namespace usub::uvent::thread
{
    void ThreadLocalStorage::push_task_inbox(std::coroutine_handle<> task)
    {
        this->inbox_q_.try_enqueue(task);
        while (!this->inbox_q_.try_enqueue(task))
            cpu_relax();
        this->inbox_seq_.fetch_add(1, std::memory_order_release);
    }
}
