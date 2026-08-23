#pragma once

#include "zecs/comp_pool.h"

namespace zecs
{

struct ViewCompGroupBase
{
    struct It
    {
        using InternalIt = CompPool2Base::EntityIt;

        inline It() : it(0), it_end(0), view(nullptr) { }
        inline It(InternalIt it_, InternalIt it_end_, const ViewCompGroupBase* view_) : it(it_), it_end(it_end_), view(view_) { next_matched(); }
        inline It(InternalIt it_end_) : it(it_end_), it_end(it_end_), view(nullptr) {}
        inline It& operator=(const It& other) { it = other.it; it_end = other.it_end; view = other.view; return *this; }
        inline It& operator++() { ++it; next_matched(); return *this; };
        inline bool operator==(const It& other) const { return it == other.it; }
        inline bool operator!=(const It& other) const { return it != other.it; }
        inline Entity operator*() const { return *it; }

    private:
        InternalIt it;
        InternalIt it_end;
        const ViewCompGroupBase* view;

        inline void next_matched() noexcept { while (it != it_end && !view->has<1>(*it)) { ++it; } }
    };

    void init(CompPool2Base** pools_ptr_, size_t pools_count_);
    inline bool empty() const noexcept { return it_begin == it_end; }
    inline It begin() noexcept { return it_begin; }
    inline It end() noexcept { return it_end; }

    template<size_t offset = 0>
    inline bool has(Entity e) const noexcept
    {
        for (size_t i = offset; i < pools_count; ++i) { if (!pools_ptr[i]->contains(e)) return false; }
        return true;
    }

private:
    size_t pools_count = 0;
    CompPool2Base** pools_ptr = nullptr;

    It it_begin;
    It it_end;
};

template<typename... T>
struct ViewCompGroup2 : public ViewCompGroupBase
{
    static_assert(sizeof...(T) > 0);
    using Pools = std::array<CompPool2Base*, sizeof...(T)>;

    ViewCompGroup2(Pools pools_)
        : pools(std::move(pools_))
    { 
        init(pools.data(), pools.size());
    }

private:
    Pools pools;
};

struct ViewCompGroupRt : public ViewCompGroupBase
{
    using Pools = CompPoolsPacked;

    ViewCompGroupRt(Pools pools_)
        : pools(std::move(pools_))
    { 
        init(pools.data(), pools.size());
    }

private:
    Pools pools;
};
} // namespace zecs
