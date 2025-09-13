//
// Created by Kirill Zhukov on 07.11.2024.
//

#include "include/uvent/pool/ThreadPool.h"

namespace usub::uvent {
    ThreadPool::ThreadPool(int size) {
        this->barrier = new std::barrier<>(size);
        for (int i = 0; i < size - 1; i++)
            this->threads.push_back(new system::Thread(this->barrier, i, system::NEW));
    }

    void ThreadPool::stop() {
        for (auto &thread: this->threads) {
            thread->stop();
            delete thread;
        }
    }

    void ThreadPool::addThread(system::ThreadLaunchMode tlm) {
        this->threads.push_back(
                new system::Thread(this->barrier, static_cast<int>(this->threads.size()), tlm));
    }

    ThreadPool::~ThreadPool() {
        this->stop();
        delete this->barrier;
    }
}