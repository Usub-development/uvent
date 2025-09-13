//
// Created by kirill on 1/29/25.
//

#ifndef UVENT_SLEEPER_H
#define UVENT_SLEEPER_H

#include <mutex>
#include <atomic>
#include <variant>
#include <condition_variable>

#ifdef UVENT_DEBUG

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#endif

namespace usub::uvent::utils::detail::thread
{
    extern int thread_count;
    extern std::atomic<bool> is_started;
}

#endif //UVENT_SLEEPER_H
