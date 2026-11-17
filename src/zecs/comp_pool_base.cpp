#include "zecs/comp_pool_base.h"
#include "zecs/types.h"

#include <cassert>
#include <cstddef>

namespace zecs
{

CompPool2Base::CompPool2Base(GUid guid_, const std::type_info* type_)
    : guid(guid_)
    , type(type_)
{
    // Add slot 0. It is never used for a real component:
    // its entity is Entity_INVALID forever.
    // So index 0 can mean "no slot".
    dense.push_back(Entity_INVALID);
    ticks.push_back(GTick_INVALID);
    patches.push_back(PatchNode());
}

DIndex CompPool2Base::dense_add(Entity e, GTick tick) noexcept
{
    const DIndex si = sparse_index_get(e);

    // Grow the sparse table lazily to cover this entity; dense_index_get() pays
    // the bounds check for entities that never got a component in this pool.
    if (si >= sparse.size())
    {
        sparse_size = si + 1;
        sparse.resize(sparse_size, DIndex_NONE);
    }

    assert(sparse[si] == DIndex_NONE);

    // A new component is the newest one: it becomes the head of the patch list.
    const PatchNode links{ patch_head, DIndex_NONE };

    DIndex di = free_head;
    if (di == DIndex_NONE)
    {
        di = static_cast<DIndex>(dense.size());
        assert(di < std::numeric_limits<DIndex>::max());

        dense.push_back(e);
        ticks.push_back(tick);
        patches.push_back(links);
    }
    else
    {
        free_head = static_cast<DIndex>(ticks[di]);

        dense[di] = e;
        ticks[di] = tick;
        patches[di] = links;
    }

    if (patch_head != DIndex_NONE)
        patches[patch_head].prev = di;

    patch_head = di;
    sparse[si] = di;
    dense_count++;

    return di;
}

void CompPool2Base::dense_rem(DIndex di) noexcept
{
    assert(dense.size() == ticks.size());
    assert(di < dense.size());

    const DIndex old_prev = patches[di].prev;
    const DIndex old_next = patches[di].next;
    const DIndex si = sparse_index_get(dense[di]);

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

void CompPool2Base::patched_emit(Entity e) noexcept
{
    on_patched.Emit(*world, e);
}
} // namespace zecs