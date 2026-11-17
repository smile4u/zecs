#include "zecs/comp_pool_base.h"
#include "zecs/types.h"

#include <cassert>
#include <cstddef>

namespace zecs
{

CompPool2Base::CompPool2Base()
{
    // Add slot 0. It is never used for a real component:
    // its entity is Entity_INVALID forever.
    // So index 0 can mean "no slot".
    dense.push_back(Entity_INVALID);
    ticks.push_back(GTick_INVALID);
    patches.push_back(PatchNode());
    dense_size = DIndex_FIRST;
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
        ticks.resize(dense_size);
        patches.resize(dense_size);

        assert(dense_size < std::numeric_limits<DIndex>::max());
        di = dense_size - 1;
    }
    else
    {
        di = free_head;
        free_head = static_cast<DIndex>(ticks[di]);
    }

    assert(dense.size() == ticks.size());
    assert(di < dense.size());

    sparse[si] = di;
    dense[di] = e;
    ticks[di] = GTick_INVALID;
    patches[di] = PatchNode();
    dense_count++;

    return di;
}

void CompPool2Base::dense_rem(DIndex di) noexcept
{
    assert(dense.size() == ticks.size());
    assert(di < dense.size());

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
    // The free list goes through ticks[], not through patches[]:
    // a removed slot keeps its old patch links, so a PatchIt that
    // stands on it can still move on.
    ticks[di] = free_head;

    assert(dense_count > 0);
    dense_count--;

    free_head = di;
}

void CompPool2Base::dense_patch(DIndex di, GTick tick) noexcept
{
    assert(di < ticks.size());

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

    ticks[patch_head] = tick;
}
} // namespace zecs