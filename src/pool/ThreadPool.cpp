//
// Created by Kirill Zhukov on 07.11.2024.
//

#include "uvent/pool/ThreadPool.h"

namespace usub::uvent {
    ThreadPool::ThreadPool(int size) : size_(size) {
        this->barrier = new std::barrier<>(size);
        system::global::detail::tls_registry = std::make_unique<thread::TLSRegistry>(this->size_);
        for (int i = 0; i < size - 1; i++)
            this->threads.push_back(new system::Thread(this->barrier, i, system::global::detail::tls_registry->getStorage(i), system::NEW));
    }

    void ThreadPool::stop() {
        for (auto &thread: this->threads) {
            thread->stop();
            delete thread;
        }
    }

    void ThreadPool::addThread(system::ThreadLaunchMode tlm) {
        this->threads.push_back(
                new system::Thread(this->barrier, static_cast<int>(this->threads.size()), system::global::detail::tls_registry->getStorage(this->threads.size()), tlm));
    }

    const thread::TLSRegistry* ThreadPool::getTLSRegistry()
    {
        return system::global::detail::tls_registry.get();
    }

    ThreadPool::~ThreadPool() {
        this->stop();
        delete this->barrier;
    }
}