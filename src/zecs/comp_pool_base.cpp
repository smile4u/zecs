#include "zecs/comp_pool_base.h"
#include "zecs/types.h"

#include <cassert>
#include <cstddef>

namespace zecs
{

CompPool2Base::CompPool2Base()
{
    // Seed the permanent guard slot at dense index 0 (see comp_pool_base.h).
    dense.push_back(Entity_INVALID);
    comps.push_back(CompNode());
    patches.push_back(PatchNode());
    dense_size = 1;
}

DIndex CompPool2Base::dense_add(Entity e) noexcept
{
    DIndex di = DIndex_NONE;
    DIndex si = static_cast<DIndex>(e);

    // Grow the sparse table lazily to cover this entity; dense_index_get() pays
    // the bounds check for entities that never got a component in this pool.
    if (si >= sparse.size())
    {
        sparse_size = si + 1;
        sparse.resize(sparse_size, DIndex_NONE);
    }

    assert(sparse[si] == DIndex_NONE);

    if (free_head == DIndex_NONE)
    {
        dense_size = dense.size() + 1;
        dense.resize(dense_size);
        comps.resize(dense_size);
        patches.resize(dense_size);

        assert(dense_size < std::numeric_limits<DIndex>::max());
        di = dense_size - 1;
    }
    else
    {
        di = free_head;
        free_head = comps[di].free_next;
    }

    assert(dense.size() == comps.size());
    assert(di < dense.size());

    sparse[si] = di;
    dense[di] = e;
    comps[di] = CompNode();
    patches[di] = PatchNode();
    dense_count++;

    return di;
}

void CompPool2Base::dense_rem(DIndex di) noexcept
{
    assert(dense.size() == comps.size());
    assert(di < dense.size());
    assert(comps[di].comp == nullptr);

    const DIndex old_prev = patches[di].prev;
    const DIndex old_next = patches[di].next;
    const DIndex si = static_cast<DIndex>(dense[di]);

    sparse[si] = DIndex_NONE;

    if (old_prev != DIndex_NONE)
        patches[old_prev].next = old_next;

    if(old_next != DIndex_NONE)
        patches[old_next].prev = old_prev;

    if (patch_head == di)
        patch_head = old_next;

    dense[di] = Entity_INVALID;
    // Push the slot onto the free-list through CompNode::free_next, NOT through
    // patches[di]: patches[] of a dead slot intentionally keeps its stale patch-list
    // linkage as a waypoint, so a PatchIt parked on this slot can still reach its
    // historical successor instead of wandering into the free-list.
    comps[di].free_next = free_head;

    assert(dense_count > 0);
    dense_count--;

    free_head = di;
}

void CompPool2Base::dense_patch(DIndex di, GTick tick) noexcept
{
    assert(di < comps.size());

    if (patch_head != di)
    {
        const DIndex old_prev = patches[di].prev;
        const DIndex old_next = patches[di].next;

        if (old_prev != DIndex_NONE)
            patches[old_prev].next = old_next;

        if(old_next != DIndex_NONE)
            patches[old_next].prev = old_prev;

        if (patch_head != DIndex_NONE)
            patches[patch_head].prev = di;

        patches[di].next = patch_head;
        patches[di].prev = DIndex_NONE;
        patch_head = di;
    }

    comps[patch_head].tick = tick;
}
} // namespace zecs