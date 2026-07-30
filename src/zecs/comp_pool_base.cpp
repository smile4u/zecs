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
    dense_info.push_back(Node());
    dense_size = 1;
}

DIndex CompPool2Base::dense_add(Entity e) noexcept
{
    DIndex di = DIndex_NONE;
    DIndex si = static_cast<DIndex>(e);

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
        dense_info.resize(dense_size);

        assert(dense_size < std::numeric_limits<DIndex>::max());
        di = dense_size - 1;
    }
    else
    {
        di = free_head;
        free_head = dense_info[free_head].next;
    }

    assert(dense.size() == dense_info.size());
    assert(di < dense.size());

    sparse[si] = di;
    dense[di] = e;
    dense_info[di] = Node();
    dense_count++;

    return di;
}

void CompPool2Base::dense_rem(DIndex di) noexcept
{
    assert(dense.size() == dense_info.size());
    assert(di < dense.size());
    assert(dense_info[di].comp == nullptr);

    const DIndex old_prev = dense_info[di].prev;
    const DIndex old_next = dense_info[di].next;
    const DIndex si = static_cast<DIndex>(dense[di]);

    sparse[si] = DIndex_NONE;

    if (old_prev != DIndex_NONE)
        dense_info[old_prev].next = old_next;

    if(old_next != DIndex_NONE)
        dense_info[old_next].prev = old_prev;

    if (patch_head == di)
        patch_head = old_next;

    dense[di] = Entity_INVALID;
    dense_info[di].next = free_head;

    assert(dense_count > 0);
    dense_count--;

    free_head = di;
}

void CompPool2Base::dense_patch(DIndex di, GTick tick) noexcept
{
    assert(di < dense_info.size());

    if (patch_head != di)
    {
        const DIndex old_prev = dense_info[di].prev;
        const DIndex old_next = dense_info[di].next;

        if (old_prev != DIndex_NONE)
            dense_info[old_prev].next = old_next;

        if(old_next != DIndex_NONE)
            dense_info[old_next].prev = old_prev;

        if (patch_head != DIndex_NONE)
            dense_info[patch_head].prev = di;

        dense_info[di].next = patch_head;
        dense_info[di].prev = DIndex_NONE;
        patch_head = di;
    }

    dense_info[patch_head].tick = tick;
}
} // namespace zecs