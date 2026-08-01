#pragma once

#include "zecs/comp_pool_base.h"
#include "zecs/types.h"

namespace zecs
{

struct ViewCompGroupRt
{
    using Pools = CompPoolsPacked;

    struct It
    {
        using InternalIt = CompPool2Base::EntityIt;

        inline It() noexcept : it(), it_end(), view(nullptr) { }
        inline It(InternalIt it_, InternalIt it_end_, const ViewCompGroupRt* view_) noexcept
            : it(it_), it_end(it_end_), view(view_)
        {
            next_matched();
        }
        inline It(InternalIt it_end_) noexcept : it(it_end_), it_end(it_end_), view(nullptr) { }
        inline It& operator++() noexcept { ++it; next_matched(); return *this; }
        inline bool operator==(const It& other) const noexcept { return it == other.it; }
        inline bool operator!=(const It& other) const noexcept { return it != other.it; }
        inline Entity operator*() const noexcept { return *it; }

    private:
        InternalIt it;
        InternalIt it_end;
        const ViewCompGroupRt* view;

        inline void next_matched() noexcept
        {
            while (it != it_end && !view->has_all(*it))
                ++it;
        }
    };

    ViewCompGroupRt(Pools pools_);

    inline bool empty() const noexcept { return it_begin == it_end; }
    inline It begin() const noexcept { return it_begin; }
    inline It end() const noexcept { return it_end; }

private:
    bool has_all(Entity e) const noexcept;

    Pools pools;
    size_t lead = 0;
    It it_begin;
    It it_end;
};

} // namespace zecs
