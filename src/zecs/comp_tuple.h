#pragma once

#include "zecs/comp_pool.h"

#include <array>
#include <utility>

namespace zecs
{

template<typename... T>
struct CompTuplBase
{
    using Pools = std::array<CompPool2Base*, sizeof...(T)>;

    CompTuplBase(CompPool2<T>*... pools_)
        : pools{ pools_... }
    { }

    Pools pools;
};

template<typename... T>
struct CompTupl : public CompTuplBase<T...>
{
    bool has_all(Entity e) const noexcept
    {
        for (const CompPool2Base* pool: CompTuplBase<T...>::pools)
        {
            if (!pool || !pool->contains(e))
                return false;
        }

        return true;
    }

    bool has_any(Entity e) const noexcept
    {
        for (const CompPool2Base* pool : CompTuplBase<T...>::pools)
        {
            if (pool && pool->contains(e))
                return true;
        }

        return false;
    }
};

} // namespace zecs
