#ifndef UVENT_SYNC_COMMON_H
#define UVENT_SYNC_COMMON_H

#include <coroutine>
#include <cstdint>
#include "uvent/sync/WaitList.h"
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
        if (tid == system::this_thread::detail::t_id && tid >= 0)
        {
            system::this_thread::detail::q.enqueue(h);
            return;
        }
#ifdef UVENT_ENABLE_REUSEADDR
        if (is_valid_thread_id(tid))
        {
            system::co_spawn_static(h, tid);
            return;
        }
        system::co_spawn(h);
#else
        system::co_spawn(h);
        if (auto* reg = system::global::detail::tls_registry.get())
            if (system::global::detail::thread_count.load(std::memory_order_relaxed) > 0)
                reg->getStorage(0)->wake_poller();
#endif
    }

    inline bool fire_waiter(Waiter* w, int32_t idx) noexcept {
        int32_t expected = -1;
        if (!w->winner->compare_exchange_strong(expected, idx, std::memory_order_acq_rel,
                                                std::memory_order_relaxed))
            return false;
        resume_on(w->h, w->tid);
        return true;
    }

    inline bool fire_waiter(Waiter* w) noexcept { return fire_waiter(w, w->index); }

    inline uint32_t select_rotation() noexcept {
        thread_local uint32_t rr = 0;
        return rr++;
    }

} // namespace usub::uvent::sync::detail

#endif // UVENT_SYNC_COMMON_H
