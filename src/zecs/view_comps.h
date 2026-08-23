#pragma once

#include "zecs/comp_pool_base.h"

namespace zecs
{
struct ViewComps
{
    using It = CompPool2Base::EntityIt;

    inline ViewComps(const CompPool2Base* pool_) : pool(pool_) { }
    inline bool empty() { return (pool) ? pool->empty() : true; }
    inline It begin() { return (pool) ? pool->entities_begin() : It(0); };
    inline It end() { return (pool) ? pool->entities_end() : It(0); };

private:
    const CompPool2Base* pool;
};

} // namespace zecs
