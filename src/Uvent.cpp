//
// Created by kirill on 11/17/24.
//

#include "uvent/Uvent.h"

namespace usub {
    Uvent::Uvent(int threadCount) : pool(threadCount), thread_count_(threadCount)
    {
        uvent::system::global::detail::thread_count = threadCount;
    }

    void Uvent::stop() {
        this->pool.stop();
    }

    void Uvent::run() {
        this->pool.addThread(uvent::system::CURRENT);
    }

    void Uvent::for_each_thread(std::function<void(int, uvent::thread::ThreadLocalStorage*)> f) const
    {
        for (int i = 0; i < this->thread_count_; i++)
            f(i, uvent::system::global::detail::tls_registry->getStorage(i));
    }
}
