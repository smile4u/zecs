#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <type_traits>

#include "zecs/guid.h"

namespace zecs
{

// A type that can be a component: an object type without const and
// volatile (see GUidT). `world.comp_get<const Position>(e)` doesn't
// compile: write `world.comp_get<Position>(e)`.
template<typename T>
concept CompT = GUidT<T>;

// A tag has no data and needs no constructor or destructor call.
// Only then a pool keeps no memory for it. Any other empty type
// is a normal component.
template<typename T>
constexpr bool is_tag_component = std::is_empty_v<T>
    && std::is_trivially_default_constructible_v<T>
    && std::is_trivially_destructible_v<T>;

// True when U stands in T... one time.
template<typename U, typename... T>
constexpr bool is_type_once = ((std::is_same_v<U, T> ? 1 : 0) + ... + 0) == 1;

// True when no type stands in T... two times.
template<typename... T>
constexpr bool are_types_unique = (is_type_once<T, T...> && ...);

// The default value of CompConfig::page_size. It is T::page_size when
// the type has it. If not, it is the number of components in about 64 KB,
// but not more than 4096.
template<typename T>
constexpr std::size_t comp_page_size_default() noexcept
{
    if constexpr (requires { T::page_size; })
        return T::page_size;
    else
        return std::min<std::size_t>(4096, std::bit_ceil(std::max<std::size_t>(1, 64 * 1024 / sizeof(T))));
}

// The default value of CompConfig::stable_pointers. It is
// T::stable_pointers when the type has it. If not, it is false.
template<typename T>
constexpr bool comp_stable_pointers_default() noexcept
{
    if constexpr (requires { T::stable_pointers; })
        return T::stable_pointers;
    else
        return false;
}

} // namespace zecs
