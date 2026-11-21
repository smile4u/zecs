#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include <memory>

// `inline` for the small functions that run on every step of a view loop.
// MSVC leaves them as calls with a plain `inline`, and then the iterator
// lives in memory, not in registers. So for MSVC it is a forced inline.
// Other compilers inline them already; forcing made the loop slower with clang.
#if defined(_MSC_VER) && !defined(__clang__)
#  define ZECS_HOT_INLINE __forceinline
#else
#  define ZECS_HOT_INLINE inline
#endif

// A field of an empty type that takes no space in its struct.
// MSVC and clang-cl ignore the standard attribute and need their own.
#if defined(_MSC_VER)
#  define ZECS_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#  define ZECS_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace zecs
{
struct World;
struct CompPool2Base;
struct EventDequeBase;

using EntityBase = uint32_t;
enum class Entity : EntityBase;

using GTick = uint64_t;
using DIndex = uint32_t; // index into a component pool's dense/sparse storage

using CompPools = std::vector<std::unique_ptr<CompPool2Base>>;
using CompPoolsPacked = std::vector<CompPool2Base*>;
using EventDeques = std::vector<std::unique_ptr<EventDequeBase>>;
using EventDequesPacked = std::vector<EventDequeBase*>;

constexpr GTick GTick_INVALID = std::numeric_limits<GTick>::max();
constexpr EntityBase EntityBase_INVALID = std::numeric_limits<EntityBase>::max();
constexpr Entity Entity_INVALID = static_cast<Entity>(EntityBase_INVALID);

// An entity ID has two parts: the index in the low bits and the generation
// in the high bits. A removed index is used again with the next generation,
// so the old ID and the new ID are different.
// Limits: the generation starts again from 0 after its last value, so a very
// old ID can be equal to a live one. No more than Entity_INDEX_MASK entities
// can live at the same time (checked by assert only).
constexpr uint32_t Entity_INDEX_BITS = 20;
constexpr EntityBase Entity_INDEX_MASK = (EntityBase(1) << Entity_INDEX_BITS) - 1;
constexpr EntityBase Entity_GEN_MASK = ~Entity_INDEX_MASK;
constexpr EntityBase Entity_INDEX_NONE = Entity_INDEX_MASK; // no real entity has this index

constexpr DIndex DIndex_NONE = 0; // 0 means "no dense slot" (used as permanent guard)
constexpr DIndex DIndex_FIRST = 1; // first real dense slot
constexpr size_t Size_INVALID = std::numeric_limits<size_t>::max();

struct TickContext
{
    TickContext() = default;
    inline TickContext(GTick since_, float dt_ = 0) : since(since_), dt(dt_) { }
    inline void skip() noexcept { since = GTick_INVALID; }

    GTick since = 0;
    float dt = 0.0f;
};

constexpr EntityBase entity_to_index(Entity e) noexcept
{
    return static_cast<EntityBase>(e) & Entity_INDEX_MASK;
}
}
