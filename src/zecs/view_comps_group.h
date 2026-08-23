#pragma once

#include "zecs/comp_pool.h"
#include "zecs/types.h"

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace zecs
{

template<typename... T>
struct ViewCompGroup
{
    static_assert(sizeof...(T) > 0);
    using Pools = std::array<CompPool2Base*, sizeof...(T)>;

    struct It
    {
        using InternalIt = CompPool2Base::EntityIt;

        inline It(InternalIt it_, InternalIt it_end_, const ViewCompGroup* view_) noexcept
            : it(it_)
            , it_end(it_end_)
            , view(view_)
        {
            next_matched();
        }

        inline It() noexcept : it(0), it_end(0), view(nullptr) { }        
        inline It(InternalIt it_end_) noexcept : it(it_end_), it_end(it_end_), view(nullptr) { }
        inline It& operator++() noexcept { ++it; next_matched(); return *this; }
        inline bool operator==(const It& other) const noexcept { return it == other.it; }
        inline bool operator!=(const It& other) const noexcept { return it != other.it; }
        inline Entity operator*() const noexcept { return *it; }

    private:
        InternalIt it;
        InternalIt it_end;
        const ViewCompGroup* view;

        inline void next_matched() noexcept
        {
            while (it != it_end && !view->has_all(*it))
                ++it;
        }
    };

    ViewCompGroup(Pools pools_, GTick* tick_)
        : pools(pools_)
        , tick(tick_)
    {
        for (size_t i = 0; i < pools.size(); ++i)
        {
            if (pools[i] == nullptr)
            {
                lead = i;
                break;
            }

            if (pools[i]->size() < pools[lead]->size())
                lead = i;
        }

        if (pools[lead])
        {
            CompPool2Base::EntityIt internal_end = pools[lead]->entities_end();
            it_begin = It(pools[lead]->entities_begin(), internal_end, this);
            it_end = It(internal_end);
        }
    }

    inline bool empty() const noexcept { return it_begin == it_end; }
    inline It begin() const noexcept { return it_begin; }
    inline It end() const noexcept { return it_end; }

    template<typename U>
    inline U* comp_get(Entity e) const noexcept
    {
        const auto* pool = pool_at<U>(e);
        return pool ? pool->get(e) : nullptr;
    }

    template<typename U>
    inline void comp_patch(Entity e) const noexcept
    {
        auto* pool = pool_at<U>(e);
        if (pool)
            pool->patch(e, (*tick)++);
    }

private:
    template<size_t... Idx>
    inline bool has_all_impl(Entity e, std::index_sequence<Idx...>) const noexcept
    {
        return ((Idx == lead || pools[Idx]->contains(e)) && ...);
    }

    inline bool has_all(Entity e) const noexcept
    {
        return has_all_impl(e, std::index_sequence_for<T...>{});
    }

    template<typename U>
    inline CompPool2<U>* pool_at(Entity e) const noexcept
    {
        constexpr size_t idx = index_of<U>();
        static_assert(idx < sizeof...(T), "U must be one of the view's component types");
        return static_cast<CompPool2<U>*>(pools[idx]);
    }

    // Position of U within T..., or Size_INVALID if U is not one of T....
    template<typename U>
    static constexpr size_t index_of() noexcept
    {
        const bool matches[] = { std::is_same_v<U, T>... };
        for (size_t i = 0; i < sizeof...(T); ++i)
        {
            if (matches[i])
                return i;
        }

        return Size_INVALID;
    }

    Pools pools;
    GTick* tick;
    size_t lead = 0;
    It it_begin;
    It it_end;
};

} // namespace zecs
