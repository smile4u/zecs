#include "zecs/view_comps_group_rt.h"

#include <utility>

namespace zecs
{

ViewCompGroupRt::ViewCompGroupRt(Pools pools_)
    : pools(std::move(pools_))
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

bool ViewCompGroupRt::has_all(Entity e) const noexcept
{
    // The pool driving iteration (lead) trivially always contains e, since e
    // only ever comes from its own dense array -- skip re-checking it.
    for (size_t i = 0; i < pools.size(); ++i)
    {
        if (i != lead && !pools[i]->contains(e))
            return false;
    }

    return true;
}

} // namespace zecs
