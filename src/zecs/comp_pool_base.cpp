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
    assert(!signal_is_running()); // see the contract of the signals

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
        assert(di <= Entity_INDEX_MASK); // the high bits of sparse[] are for the generation

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
    sparse[si] = sparse_cell_make(e, di);
    dense_count++;

    return di;
}

bool CompPool2Base::remove(Entity e) noexcept
{
    assert(!signal_is_running()); // see the contract of the signals

    const DIndex di = dense_index_get(e);
    if (di == DIndex_NONE)
        return false;

    // The handlers do not add or remove components of this pool,
    // so `di` is still correct after them.
    signal_emit(on_removed, e);

    comp_slot_rem(di, di);
    dense_rem(di);
    return true;
}

void CompPool2Base::dense_rem(DIndex di) noexcept
{
    assert(dense.size() == ticks.size());
    assert(di < dense.size());

    sparse[sparse_index_get(dense[di])] = DIndex_NONE;
    patch_rem(di);

    dense[di] = Entity_INVALID;
    // The free list goes through ticks[], not through patches[]:
    // a removed slot keeps its old patch links, so a PatchIt that
    // stands on it can still move on.
    ticks[di] = free_head;

    assert(dense_count > 0);
    dense_count--;

    free_head = di;
}

void CompPool2Base::patch_rem(DIndex di) noexcept
{
    const DIndex old_prev = patches[di].prev;
    const DIndex old_next = patches[di].next;

    if (old_prev != DIndex_NONE)
        patches[old_prev].next = old_next;

    if (old_next != DIndex_NONE)
        patches[old_next].prev = old_prev;

    if (patch_head == di)
        patch_head = old_next;
}

void CompPool2Base::dense_move(DIndex from, DIndex to) noexcept
{
    const Entity e = dense[from];
    const PatchNode links = patches[from];

    dense[to] = e;
    ticks[to] = ticks[from];
    patches[to] = links;
    sparse[sparse_index_get(e)] = sparse_cell_make(e, to);

    // The neighbours in the patch list must point to the new slot.
    if (links.prev != DIndex_NONE)
        patches[links.prev].next = to;
    else
        patch_head = to;

    if (links.next != DIndex_NONE)
        patches[links.next].prev = to;
}

void CompPool2Base::compact() noexcept
{
    // No empty slots: nothing to do, and the arrays are not copied.
    if (free_head == DIndex_NONE)
        return;

    // Cut the empty end of sparse[]. It is done before the moves,
    // so a pool that can not move its components gets it too.
    // TODO: empty cells in the middle stay. To free them sparse[] must be
    // made of pages. That adds one memory read to every search: measure first.
    while (sparse_size > 0 && sparse[sparse_size - 1] == DIndex_NONE) --sparse_size;
    sparse.resize(sparse_size);
    sparse.shrink_to_fit();

    // `to` looks for an empty slot from the start,
    // `from` looks for a used slot from the end.
    DIndex to = DIndex_FIRST;
    DIndex from = static_cast<DIndex>(dense.size()) - 1;

    while (true)
    {
        while (to < from && dense[to] != Entity_INVALID) ++to;
        while (from > to && dense[from] == Entity_INVALID) --from;

        if (to >= from)
            break;

        // The first move fails or none does, so nothing is half done here.
        if (!comp_slot_move(from, to))
            return;

        dense_move(from, to);
        dense[from] = Entity_INVALID;
    }

    // `to` is the last used slot or the first empty one.
    const DIndex size = (to < dense.size() && dense[to] != Entity_INVALID) ? to + 1 : to;
    assert(size - DIndex_FIRST == dense_count);

    dense.resize(size);
    ticks.resize(size);
    patches.resize(size);

    dense.shrink_to_fit();
    ticks.shrink_to_fit();
    patches.shrink_to_fit();

    free_head = DIndex_NONE;
    comp_slots_trim(size);
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
    signal_emit(on_patched, e);
}
} // namespace zecs