//
// Created by kirill on 21/08/26.
//

#ifndef UVENT_SYSTEM_STACK_GUARD_H
#define UVENT_SYSTEM_STACK_GUARD_H

#include <cstddef>
#include <cstdint>

#include "uvent/system/Settings.h"

namespace usub::uvent::system::stack_guard
{
    inline thread_local const char* t_stack_base = nullptr;

    inline void set_stack_base(const void* p) noexcept { t_stack_base = static_cast<const char*>(p); }

    inline bool stack_too_deep() noexcept
    {
        char probe;
        const char* here = &probe;
        if (!t_stack_base) [[unlikely]]
            return false;
        const std::size_t depth = t_stack_base > here ? std::size_t(t_stack_base - here) : std::size_t(here - t_stack_base);
        return depth > settings::max_transfer_stack_depth;
    }
} // namespace usub::uvent::system::stack_guard

#endif // UVENT_SYSTEM_STACK_GUARD_H
