//
// Created by Kirill Zhukov on 07.11.2024.
//

#ifndef UVENT_THREAD_H
#define UVENT_THREAD_H

#include <ctime>
#include <thread>
#include <chrono>
#include <barrier>
#include <functional>
#include "uvent/system/Defines.h"
#include "uvent/utils/thread/ThreadStats.h"
#include "uvent/utils/timer/HighPerfomanceTimer.h"
#include "uvent/system/SystemContext.h"
#include "uvent/base/Predefines.h"

namespace usub::uvent::system
{
    enum ThreadLaunchMode
    {
        CURRENT,
        NEW
    };

    class Thread
    {
    public:
        friend class ThreadPool;

        Thread(std::barrier<>* barrier, int index, ThreadLaunchMode tlm);

        Thread(Thread&&) noexcept = default;

        Thread& operator=(Thread&&) noexcept = default;

        Thread(const Thread&) = delete;

        Thread& operator=(const Thread&) = delete;

        ~Thread() = default;

        bool stop();

    private:
        void threadFunction(std::stop_token& token);

    private:
        int index_;
        std::jthread thread_;
        std::barrier<>* barrier;
        std::stop_token stop_token{};
        ThreadLaunchMode tlm{NEW};
        std::vector<std::coroutine_handle<>> tmp_tasks_;
        std::vector<net::SocketHeader*> tmp_sockets_;
    };
}


#endif //UVENT_THREAD_H
