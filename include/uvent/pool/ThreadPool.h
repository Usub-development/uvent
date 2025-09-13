//
// Created by Kirill Zhukov on 07.11.2024.
//

#ifndef UVENT_THREADPOOL_H
#define UVENT_THREADPOOL_H

#include <cmath>
#include "include/uvent/system/Thread.h"
#include "include/uvent/system/Defines.h"

namespace usub::uvent
{
    class ThreadPool
    {
    public:
        friend class Uvent;

        explicit ThreadPool(int size);

        ~ThreadPool();

        void stop();

        void addThread(system::ThreadLaunchMode tlm);

    private:
        std::barrier<>* barrier;
        std::vector<system::Thread*> threads;
    };
}


#endif //UVENT_THREADPOOL_H
