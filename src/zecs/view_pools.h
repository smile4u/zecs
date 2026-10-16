#pragma once

#include "zecs/comp_pool_base.h"

namespace zecs
{

struct ViewPoolsIter
{
    using InternalIt = CompPoolsPacked::const_iterator;

    inline ViewPoolsIter(InternalIt it_) : it(it_) { }
    inline ViewPoolsIter& operator++() { ++it; return *this; }
    inline bool operator==(const ViewPoolsIter& other) const { return it == other.it; }
    inline bool operator!=(const ViewPoolsIter& other) const { return it != other.it; }
    inline CompPool2Base* operator*() const { return *it; }

private:
    InternalIt it;
};

struct ViewPools
{
    using It = ViewPoolsIter;

    ViewPools(const ViewPools&) = delete;
    ViewPools(ViewPools&&) = delete;
    ViewPools& operator=(const ViewPools&) = delete;
    ViewPools& operator=(ViewPools&&) = delete;

    inline ViewPools(const CompPoolsPacked& pools_) : pools(pools_) { }
    inline bool empty() const { return pools.empty(); }
    inline It begin() { return pools.begin(); }
    inline It end() { return pools.end(); }

private:
    const CompPoolsPacked& pools;
};
} // namespace zecs
