#pragma once

#include "zecs/comp_pool_base.h"

namespace zecs
{
struct ViewComps
{
    using It = CompPool2Base::EntityIt;

    ViewComps(const ViewComps&) = delete;
    ViewComps(ViewComps&&) = delete;
    ViewComps& operator=(const ViewComps&) = delete;
    ViewComps& operator=(ViewComps&&) = delete;

    inline ViewComps(const CompPool2Base* pool_) : pool(pool_) { }
    inline bool empty() { return (pool) ? pool->empty() : true; }
    inline It begin() { return (pool) ? pool->entities_begin() : It(); };
    inline It end() { return It(); };

private:
    const CompPool2Base* pool;
};

} // namespace zecs
