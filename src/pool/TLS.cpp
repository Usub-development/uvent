//
// Created by root on 10/21/25.
//

#include <uvent/pool/TLS.h>

namespace usub::uvent::thread
{
    void ThreadLocalStorage::push_task_inbox(std::coroutine_handle<> task)
    {
        this->inbox_q_.try_enqueue(task);
        this->is_added_new_.store(true, std::memory_order_seq_cst);
    }
}
