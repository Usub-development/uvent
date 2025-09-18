//
// Created by kirill on 1/29/25.
//

#include "uvent/utils/thread/ThreadStats.h"

namespace usub::uvent::utils::detail::thread
{
    int thread_count{0};
    std::atomic<bool> is_started{false};
}
