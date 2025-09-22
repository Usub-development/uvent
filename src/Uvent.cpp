//
// Created by kirill on 11/17/24.
//

#include "uvent/Uvent.h"

namespace usub {
    Uvent::Uvent(int threadCount) : pool(threadCount) {
        uvent::utils::detail::thread::thread_count = threadCount;
    }

    void Uvent::stop() {
        this->pool.stop();
    }

    void Uvent::run() {
        this->pool.addThread(uvent::system::CURRENT);
    }
}