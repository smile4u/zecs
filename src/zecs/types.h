#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include <memory>

namespace zecs
{
struct World;
struct CompPool2Base;
struct EventDequeBase;

using EntityBase = uint32_t;
enum class Entity : EntityBase;

using GTick = uint64_t;
using LLIndex = uint32_t; // linked list index

using CompPools = std::vector<std::unique_ptr<CompPool2Base>>;
using CompPoolsPacked = std::vector<CompPool2Base*>;
using EventDeques = std::vector<std::unique_ptr<EventDequeBase>>;
using EventDequesPacked = std::vector<EventDequeBase*>;

inline GTick globalTick = 0;

constexpr GTick GTick_INVALID = std::numeric_limits<GTick>::max();
constexpr Entity Entity_INVALID = static_cast<Entity>(std::numeric_limits<EntityBase>::max());
constexpr LLIndex LLIndex_INVALID = std::numeric_limits<LLIndex>::max();
constexpr size_t Size_INVALID = std::numeric_limits<size_t>::max();

struct TickContext
{
    TickContext() = default;
    inline TickContext(GTick since_, float dt_ = 0) : since(since_), dt(dt_) { }
    inline void skip() noexcept { since = GTick_INVALID; }

    GTick since = 0;
    float dt = 0.0f;
};

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


}
