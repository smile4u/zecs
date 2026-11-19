#pragma once

#include <type_traits>

namespace zecs
{

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

} // namespace zecs
