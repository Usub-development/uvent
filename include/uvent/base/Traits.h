#ifndef UVENT_BASE_TRAITS_H
#define UVENT_BASE_TRAITS_H

#include <type_traits>

namespace usub::uvent
{
    template <class T>
    struct is_thread_affine : std::false_type
    {
    };

    template <class T>
    inline constexpr bool is_thread_affine_v = is_thread_affine<T>::value;
} // namespace usub::uvent

#endif // UVENT_BASE_TRAITS_H
