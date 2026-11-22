#pragma once

#include "zecs/traits.h"

#include <cstddef>

// This file describes everything that a user can change in ZECS.

namespace zecs
{

// Settings of one component type. There are two ways to set them.
//
// 1. A static member in the component:
//      struct Particle
//      {
//          static constexpr size_t page_size = 256;
//          static constexpr bool stable_pointers = true;
//          ...
//      };
//
// 2. A specialization of CompConfig, for a type that you can't change.
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
// Some behavior isn't a setting. It comes from the type itself:
//   - a tag (see is_tag_component in traits.h) takes no memory in a pool;
//   - compact() moves only the components that have a move or copy constructor;
//   - a component is added without arguments only with a default constructor.
template<typename T>
struct CompConfigDefault
{
    // Number of components in one page of a pool. It must be a power of two.
    // Default: about 64 KB of memory, but not more than 4096 components.
    // The size in bytes is limited so that a pool of big components doesn't
    // take a lot of memory for its first component. The number is rounded up
    // to a power of two: with pages smaller than 64 KB adding components
    // is slower.
    static constexpr std::size_t page_size = comp_page_size_default<T>();

    // False (the default): the pool has no empty slots. When a component
    // is removed, the last component of the pool moves into its slot.
    // So a pass over the pool visits only components. Two limits:
    //   - A pointer to a component is valid only until a component of
    //     the same type is removed.
    //   - In a loop over a view, the body can remove this component only
    //     from the current entity or from an entity that the loop visited
    //     already. If the body removes it from another entity, the loop
    //     can visit some entity two times. To remove such components,
    //     put the entities in a list and remove them after the loop.
    //     A group view goes over one of its pools, and we don't know
    //     which one. So follow this rule for every type of the view.
    //     A view of patched components doesn't have this limit.
    //
    // True: components don't move. A pointer to a component is valid until
    // this component is removed or compact() is called. A loop body can
    // remove any component. The cost: a removed component leaves an empty
    // slot. Empty slots stay until compact(), and every pass visits them.
    // A type that can't be moved must set it to true.
    static constexpr bool stable_pointers = comp_stable_pointers_default<T>();
};

template<typename T>
struct CompConfig : CompConfigDefault<T> {};

} // namespace zecs
