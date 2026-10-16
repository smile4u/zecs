#pragma once

#include "zecs/comp_pool_base.h"
#include "zecs/view_comps_group_base.h"

#include <array>
#include <type_traits>
#include <utility>

namespace zecs
{

// Hot-path typed view. Pool count and membership checks are compile-time fixed;
// runtime/debug groups have an independent implementation in view_comps_group_rt.h.
template<typename... T>
struct ViewCompGroup2 : private ViewCompGroupBase
{
    static constexpr size_t POOL_COUNT = sizeof...(T);
    static_assert(POOL_COUNT > 0);
    using Pools = std::array<CompPool2Base*, POOL_COUNT>;

    struct It
    {
        using InternalIt = CompPool2Base::EntityIt;

        inline It() noexcept : it(InternalIt::sentinel()), it_end(DIndex_NONE) { }
        inline It(InternalIt it_, InternalIt it_end_, const ViewCompGroup2* view_) noexcept : it(it_), it_end(it_end_.index_get())
        {
            if constexpr (POOL_COUNT > 1)
                view = view_;
            next_matched();
        }
        inline It& operator++() noexcept { ++it; next_matched(); return *this; }
        inline bool operator==(const It& other) const noexcept
        {
            const bool at_end = it.index_get() == it_end;
            const bool other_at_end = other.it.index_get() == other.it_end;
            // Keep traversal state invariant, including at exhaustion. Bitwise
            // boolean operations keep arbitrary comparisons branch-free; a
            // known end iterator reduces this to the snapshot-bound check.
            return (at_end == other_at_end) & (at_end | (it == other.it));
        }
        inline bool operator!=(const It& other) const noexcept { return !(*this == other); }
        inline Entity operator*() const noexcept { return *it; }

    private:
        InternalIt it;
        // A scalar bound avoids storing a second full EntityIt. Keeping it
        // separate also lets the optimizer hoist it out of the traversal loop.
        DIndex it_end;
        struct NoView {};
        [[no_unique_address]] std::conditional_t<POOL_COUNT == 1, NoView, const ViewCompGroup2*> view{};

        inline void next_matched() noexcept
        {
            // begin() already established that every pool exists. A one-pool
            // group needs no membership filtering at all.
            if constexpr (POOL_COUNT > 1)
                while (it.index_get() != it_end && !view->template matches<1>(*it)) { ++it; }
        }
    };

    ViewCompGroup2(Pools pools_) noexcept
        : pools(std::move(pools_))
    {
        if constexpr (POOL_COUNT > 1)
            select_lead(pools.data(), POOL_COUNT);
    }

    inline bool empty() const noexcept { return begin() == end(); }

    // Each pass snapshots the lead pool's current dense bounds. The view owns
    // only its pool pointers; active iterators require it to stay in place.
    inline It begin() const noexcept
    {
        if (pools[0] == nullptr)
            return It();

        const auto* lead = pools[0];
        return It(lead->entities_begin(), lead->entities_end(), this);
    }

    // The bound belongs to the begin iterator, so even repeated end() calls
    // during a pass cannot extend it when new dense slots are appended.
    inline It end() const noexcept { return It(); }

    inline bool has(Entity e) const noexcept
    {
        if (pools[0] == nullptr)
            return false;

        return matches<0>(e);
    }

private:
    template<size_t offset, size_t... Indices>
    inline bool matches_impl(Entity e, std::index_sequence<Indices...>) const noexcept
    {
        return (pools[offset + Indices]->contains(e) && ...);
    }

    template<size_t offset>
    inline bool matches(Entity e) const noexcept
    {
        if constexpr (offset <= POOL_COUNT)
            return matches_impl<offset>(e, std::make_index_sequence<POOL_COUNT - offset>{});
        else
            static_assert(offset <= POOL_COUNT, "Component offset exceeds pool count");
    }

    Pools pools;
};

} // namespace zecs
