//
// Created by kirill on 11/17/24.
//

#ifndef UVENT_UVENT_H
#define UVENT_UVENT_H

#include <cmath>
#include "include/uvent/pool/ThreadPool.h"

namespace usub {
    class Uvent : std::enable_shared_from_this<Uvent> {
    public:
        explicit Uvent(int threadCount);

        void stop();

        void run();

    private:
        uvent::ThreadPool pool;
    };
}

#endif //UVENT_UVENT_H
