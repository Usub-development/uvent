#ifndef UVENT_SYSTEM_INTROSPECTION_H
#define UVENT_SYSTEM_INTROSPECTION_H

#include <cstdint>
#include <cstdio>
#include <vector>

namespace usub::uvent::detail
{
    class AwaitableFrameBase;
}

namespace usub::uvent::introspection
{
    struct TaskInfo
    {
        const void* frame{nullptr};
        const void* parent{nullptr};
        const char* name{nullptr};
        const char* wait_reason{nullptr};
        uint64_t trace_id{0};
        int thread_id{-1};
        bool cancel_requested{false};
        bool has_cancel_hook{false};
        uint64_t age_ns{0};
        uint64_t waiting_ns{0};
    };

#ifdef UVENT_TASK_INTROSPECTION
    namespace detail
    {
        struct Shard;

        uint64_t now_ns() noexcept;

        void register_frame(uvent::detail::AwaitableFrameBase* f) noexcept;

        void unregister_frame(uvent::detail::AwaitableFrameBase* f) noexcept;
    } // namespace detail

    std::vector<TaskInfo> snapshot();

    std::size_t live_count() noexcept;

    void dump(std::FILE* out = nullptr);
#else
    inline std::vector<TaskInfo> snapshot() { return {}; }

    inline std::size_t live_count() noexcept { return 0; }

    inline void dump(std::FILE* out = nullptr)
    {
        auto* o = out ? out : stderr;
        std::fputs("uvent: task introspection disabled (build with UVENT_TASK_INTROSPECTION)\n", o);
    }
#endif
} // namespace usub::uvent::introspection

#endif // UVENT_SYSTEM_INTROSPECTION_H
