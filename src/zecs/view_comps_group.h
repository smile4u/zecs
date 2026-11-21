#pragma once

#include "zecs/comp_pool_base.h"
#include "zecs/query_scope.h"
#include "zecs/traits.h"
#include "zecs/view_comps_group_base.h"

#include <array>
#include <cassert>
#include <type_traits>

namespace zecs
{

// Hot-path typed view. Pool count and membership checks are compile-time fixed;
// runtime/debug groups have an independent implementation in view_comps_group_rt.h.
template<typename... T>
struct ViewCompGroup2 : private ViewCompGroupBase
{
    static constexpr size_t POOL_COUNT = sizeof...(T);
    static_assert(POOL_COUNT > 0);
    static_assert(are_types_unique<T...>, "a component type is given two times");
    using Pools = std::array<CompPool2Base*, POOL_COUNT>;

    struct It
    {
        using InternalIt = CompPool2Base::EntityIt;

        inline It() noexcept = default;
        inline It(InternalIt it_, const ViewCompGroup2* view_) noexcept : it(it_)
        {
            if constexpr (POOL_COUNT > 1)
                view = view_;
            next_matched();
        }
        inline It& operator++() noexcept
        {
            // A one-pool group has no check of other pools, so the pool
            // iterator must skip empty slots itself.
            if constexpr (POOL_COUNT > 1)
                it.step();
            else
                ++it;

            next_matched();
            return *this;
        }
        inline bool operator==(const It& other) const noexcept { return it == other.it; }
        inline Entity operator*() const noexcept { return *it; }

    private:
        InternalIt it;
        struct NoView {};

        // A one-pool group does not use the view, so the field is empty there.
        // The attribute lets it take no space: without it the iterator
        // is 8 bytes bigger than the pool iterator.
        ZECS_NO_UNIQUE_ADDRESS std::conditional_t<POOL_COUNT == 1, NoView, const ViewCompGroup2*> view{};

        ZECS_HOT_INLINE void next_matched() noexcept
        {
            // begin() already established that every pool exists. A one-pool
            // group needs no membership filtering at all.
            // An empty slot keeps Entity_INVALID, and no pool contains it.
            // So the check of the other pools skips empty slots too.
            if constexpr (POOL_COUNT > 1)
                while (!it.at_end() && !view->scope.template has_all<1>(*it)) { it.step(); }

            assert(it.at_end() || *it != Entity_INVALID);
        }
    };

    ViewCompGroup2(Pools pools_) noexcept
        : scope(pools_)
    {
        if constexpr (POOL_COUNT > 1)
            scope.move_leading_pool_front();
    }

    inline bool empty() const noexcept { return begin() == end(); }

    // Each pass snapshots the lead pool's current dense bounds. The view owns
    // only its pool pointers; active iterators require it to stay in place.
    inline It begin() const noexcept
    {
        const CompPool2Base* lead = scope.pools[0];
        if (lead == nullptr)
            return It();

        return It(lead->entities_begin(), this);
    }

    // The bound belongs to the begin iterator, so even repeated end() calls
    // during a pass cannot extend it when new dense slots are appended.
    inline It end() const noexcept { return It(); }

    inline bool has(Entity e) const noexcept
    {
        if (scope.pools[0] == nullptr)
            return false;

        return scope.has_all(e);
    }

private:
    // The first pool is the lead: the smallest one, and the pass goes over it.
    // Every entity of it is checked in the other pools.
    // The first pool is nullptr when a pool is missing: the view is empty.
    QueryScopeBase<POOL_COUNT> scope;
};

} // namespace zecs
