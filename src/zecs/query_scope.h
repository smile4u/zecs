#pragma once

#include "zecs/comp_pool_base.h"
#include "zecs/exports.h"
#include "zecs/traits.h"

#include <array>
#include <cstddef>
#include <utility>

namespace zecs
{

// Moves the smallest pool to the first place. A missing pool (nullptr)
// goes first too. A view goes over the first pool, so its pass is the shortest.
ZECS_EXPORT void move_leading_pool_front(CompPool2Base** pools, size_t count) noexcept;

// N pools and the checks of an entity in all of them at once.
// It knows only the number of the component types, not the types:
// a group view moves its lead pool to the first place when it is made,
// so a place here can not belong to a type.
template<size_t N>
struct QueryScopeBase
{
    using Pools = std::array<CompPool2Base*, N>;

protected:
    friend struct World;
    template<typename... T>
    friend struct ViewCompGroup2;

    // The checks do not test for nullptr. World::query_scope() makes
    // the pools that are missing. A view tests its pools itself.
    inline QueryScopeBase(const Pools& pools_) noexcept
        : pools(pools_)
    { }

    // See move_leading_pool_front().
    inline void move_leading_pool_front() noexcept
    {
        // One pool: nothing to move, so there is no call at all.
        if constexpr (N > 1)
            zecs::move_leading_pool_front(pools.data(), N);
    }

    // The pools are checked in their order, with a stop at the first "no".
    // `Offset` is the number of the first pools to skip: a view does not
    // check its lead pool, the entity comes from it.
    template<size_t Offset = 0>
    inline bool has_all(Entity e) const noexcept
    {
        static_assert(Offset <= N);
        return has_all_impl<Offset>(e, std::make_index_sequence<N - Offset>{});
    }

    // The same with a stop at the first "yes".
    inline bool has_any(Entity e) const noexcept { return has_any_impl(e, std::make_index_sequence<N>{}); }

    Pools pools;

private:
    template<size_t Offset, size_t... I>
    inline bool has_all_impl(Entity e, std::index_sequence<I...>) const noexcept
    {
        return (pools[Offset + I]->contains(e) && ...);
    }

    template<size_t... I>
    inline bool has_any_impl(Entity e, std::index_sequence<I...>) const noexcept
    {
        return (pools[I]->contains(e) || ...);
    }
};

// Tells the query_* functions of World which components to work with.
// It keeps their pools, so the pools are searched in the world one time.
// It can be kept and copied: it is valid while the world lives.
template<typename... T>
struct QueryScope : QueryScopeBase<sizeof...(T)>
{
    static_assert(sizeof...(T) > 0);
    static_assert(are_types_unique<T...>, "a component type is given two times");

private:
    friend struct World;
    using Base = QueryScopeBase<sizeof...(T)>;

    // Only World makes a scope: see World::query_scope().
    inline QueryScope(const typename Base::Pools& pools_) noexcept
        : Base(pools_)
    { }
};

} // namespace zecs
