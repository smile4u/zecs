#pragma once

#include <cstddef>
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

template<typename T>
struct TransparentHasher
{
    using is_transparent = void; // enable heterogeneous overloads
    using is_avalanching = void; // mark class as high quality avalanching hash

    template<typename U>
    constexpr size_t operator()(U&& v) const noexcept { return static_cast<size_t>(v); }
};

template<typename T>
struct TransparentEqualTo
{
    using is_transparent = void; // enable heterogeneous overloads
    using is_avalanching = void; // mark class as high quality avalanching hash

    constexpr bool operator()(const T& l, const T& r) const noexcept { return l == r; }
};

} // namespace zecs
