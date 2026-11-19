#include "zecs/view_comps_group_rt.h"
#include "zecs/query_scope.h"
#include "zecs/world.h"

#include <algorithm>
#include <utility>

namespace zecs
{

ViewCompGroupRt::ViewCompGroupRt(Pools pools_)
    : count(pools_.size())
{
    if (count > INLINE_CAPACITY)
        pools = std::move(pools_);
    else
        std::copy(pools_.begin(), pools_.end(), std::get<InlinePools>(pools).begin());

    move_leading_pool_front(pool_data(), count);
}

ViewCompGroupRt::ViewCompGroupRt(World& world, const GUid* guids, size_t count_)
    : count(count_)
{
    if (count > INLINE_CAPACITY)
        pools.emplace<Pools>(count);

    auto* ptr = pool_data();
    for (size_t i = 0; i < count; ++i)
        ptr[i] = world.comp_pool_get(guids[i]);

    move_leading_pool_front(ptr, count);
}

CompPool2Base** ViewCompGroupRt::pool_data() noexcept
{
    if (auto* local = std::get_if<InlinePools>(&pools))
        return local->data();
    return std::get<Pools>(pools).data();
}

CompPool2Base* const* ViewCompGroupRt::pool_data() const noexcept
{
    if (const auto* local = std::get_if<InlinePools>(&pools))
        return local->data();
    return std::get<Pools>(pools).data();
}

ViewCompGroupRt::It ViewCompGroupRt::begin() const noexcept
{
    if (count == 0)
        return It();

    const auto* lead = pool_data()[0];
    if (lead == nullptr)
        return It();

    return It(lead->entities_begin(), this);
}

void ViewCompGroupRt::It::next_matched() noexcept
{
    // begin() guarantees a nonempty list of existing pools. Resolve storage
    // once per advance, rather than checking the public has() preconditions
    // and the inline/heap alternative again for every rejected candidate.
    const auto* ptr = view->pool_data();
    const size_t count = view->count;
    for (; !it.at_end(); ++it)
    {
        const Entity e = *it;
        size_t i = 1;
        while (i < count && ptr[i]->contains(e)) ++i;
        if (i == count)
            return;
    }
}

} // namespace zecs
