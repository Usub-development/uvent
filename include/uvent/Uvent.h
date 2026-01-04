//
// Created by kirill on 11/17/24.
//

#ifndef UVENT_UVENT_H
#define UVENT_UVENT_H

#include <cmath>
#include "uvent/net/Socket.h"
#include "uvent/pool/ThreadPool.h"
#include "uvent/system/SystemContext.h"

namespace usub {
    class Uvent : std::enable_shared_from_this<Uvent> {
    public:
        explicit Uvent(int threadCount);

        void stop();

        void run();

        void for_each_thread(std::function<void(int, uvent::thread::ThreadLocalStorage*)> f) const;

    private:
        int thread_count_;
        uvent::ThreadPool pool;
    };
}

#endif //UVENT_UVENT_H
