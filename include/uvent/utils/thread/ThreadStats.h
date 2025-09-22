//
// Created by kirill on 1/29/25.
//

#ifndef UVENT_SLEEPER_H
#define UVENT_SLEEPER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <variant>

#ifdef UVENT_DEBUG

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#endif

namespace usub::uvent::utils::detail::thread {
extern int thread_count;
extern std::atomic<bool> is_started;
} // namespace usub::uvent::utils::detail::thread

#endif // UVENT_SLEEPER_H
