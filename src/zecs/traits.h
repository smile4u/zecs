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

} // namespace zecs
