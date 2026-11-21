#pragma once

#include "zecs/traits.h"

#include <cstddef>

// This file describes everything that a user can change in zecs.

namespace zecs
{

// Settings of one component type. There are two ways to set them.
//
// 1. A static member in the component:
//      struct Particle
//      {
//          static constexpr size_t page_size = 256;
//          ...
//      };
//
// 2. A specialization of CompConfig, for a type that you can not change.
//    CompConfigDefault gives the default values of the other settings:
//      template<>
//      struct zecs::CompConfig<Particle> : zecs::CompConfigDefault<Particle>
//      {
//          static constexpr size_t page_size = 256;
//      };
//    Write it in the header of the component, right after the type.
//    Every file that uses the component must see it. If one file does
//    not see it, that file gets another page size, and the pool breaks.
//
// Some behavior is not a setting. It comes from the type itself:
//   - a tag (see is_tag_component in traits.h) takes no memory in a pool;
//   - compact() moves only the components that have a move or copy constructor;
//   - a component is added without arguments only with a default constructor.
template<typename T>
struct CompConfigDefault
{
    // Number of components in one page of a pool. It must be a power of two.
    // Default: about 64 KB of memory, but not more than 4096 components.
    // The size in bytes is limited so that a pool of big components does not
    // take a lot of memory for its first component. The number is rounded up
    // to a power of two: with pages smaller than 64 KB adding components
    // was measurably slower.
    static constexpr std::size_t page_size = comp_page_size_default<T>();
};

template<typename T>
struct CompConfig : CompConfigDefault<T> {};

} // namespace zecs
