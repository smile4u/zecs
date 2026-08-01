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

    // Snapshot of one pool's lookup arrays for hot loops. Grabbed once at view
    // construction, so component stores can't alias-force reloads of pool
    // fields on every step. Invalidated by structural pool changes (add/remove)
    // — same contract as the iterators handed out by the pool.
    struct PoolInfo
    {
        const DIndex* sparse = nullptr;
        size_t sparse_size = 0;
        void* const* comps = nullptr;
    };

    using PoolInfos = std::array<PoolInfo, sizeof...(T)>;

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

        inline It() noexcept : it(), it_end(), view(nullptr) { }
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

    ViewCompGroup(Pools pools_)
    {
        for (size_t i = 0; i < pools_.size(); ++i)
        {
            const CompPool2Base* pool = pools_[i];
            infos[i] = (pool)
                ? PoolInfo{ pool->sparse_data_get(), pool->sparse_size_get(), pool->comps_data_get() }
                : null_info;
        }

        for (size_t i = 0; i < pools_.size(); ++i)
        {
            if (pools_[i] == nullptr)
            {
                lead = i;
                break;
            }

            if (pools_[i]->size() < pools_[lead]->size())
                lead = i;
        }

        if (pools_[lead])
        {
            CompPool2Base::EntityIt internal_end = pools_[lead]->entities_end();
            it_begin = It(pools_[lead]->entities_begin(), internal_end, this);
            it_end = It(internal_end);
        }
    }

    inline bool empty() const noexcept { return it_begin == it_end; }
    inline It begin() const noexcept { return it_begin; }
    inline It end() const noexcept { return it_end; }

    template<typename U>
    inline U* comp_get(Entity e) const noexcept
    {
        constexpr size_t idx = index_of<U>();
        static_assert(idx < sizeof...(T), "U must be one of the view's component types");
        const PoolInfo& c = infos[idx];
        const DIndex di = info_di(c, e);
        // di == DIndex_NONE (или null-пул) ведёт на guard-слот comps[0] == nullptr
        return static_cast<U*>(c.comps[di]);
    }

private:
    static inline DIndex info_di(const PoolInfo& c, Entity e) noexcept
    {
        const DIndex si = static_cast<DIndex>(e);
        return (si < c.sparse_size) ? c.sparse[si] : DIndex_NONE;
    }

    static inline bool info_contains(const PoolInfo& c, Entity e) noexcept
    {
        return info_di(c, e) != DIndex_NONE;
    }

    template<size_t... Idx>
    inline bool has_all_impl(Entity e, std::index_sequence<Idx...>) const noexcept
    {
        return ((Idx == lead || info_contains(infos[Idx], e)) && ...);
    }

    inline bool has_all(Entity e) const noexcept
    {
        return has_all_impl(e, std::index_sequence_for<T...>{});
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

    // Guard-слот для null-пулов: comp_get индексирует comps без ветки на
    // nullptr, для отсутствующего пула попадает сюда (comps[0] == nullptr).
    inline static void* const null_comps[1] = { nullptr };
    inline static const PoolInfo null_info{ nullptr, 0, null_comps };

    PoolInfos infos;
    size_t lead = 0;
    It it_begin;
    It it_end;
};

} // namespace zecs
