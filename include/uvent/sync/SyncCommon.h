#ifndef UVENT_SYNC_COMMON_H
#define UVENT_SYNC_COMMON_H

#include <coroutine>
#include <cstdint>
#include "uvent/system/SystemContext.h"

namespace usub::uvent::sync::detail {

    inline int current_thread_id() noexcept {
        return static_cast<int>(system::this_thread::detail::t_id);
    }

    inline bool is_valid_thread_id(int tid) noexcept {
        return tid >= 0
            && static_cast<uint32_t>(tid) < system::global::detail::thread_count;
    }

    inline void resume_on(std::coroutine_handle<> h, int tid) noexcept {
        if (is_valid_thread_id(tid))
            system::co_spawn_static(h, tid);
        else
            system::co_spawn(h);
    }

} // namespace usub::uvent::sync::detail

#endif // UVENT_SYNC_COMMON_H
