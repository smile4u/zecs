#include "zecs/comp_pool_base.h"
#include "zecs/types.h"

#include <cassert>
#include <cstddef>

namespace zecs
{

LLIndex CompPool2Base::dense_add(Entity e) noexcept
{
    LLIndex di = LLIndex_INVALID;
    LLIndex si = static_cast<LLIndex>(e);

    if (si >= sparse.size())
    {
        sparse_size = si + 1;
        sparse.resize(sparse_size, LLIndex_INVALID);
    }

    assert(sparse[si] == LLIndex_INVALID);

    if (free_head == LLIndex_INVALID)
    {
        dense_size = dense.size() + 1;
        dense.resize(dense_size);
        dense_info.resize(dense_size);

        assert(dense_size < LLIndex_INVALID);
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

void CompPool2Base::dense_rem(LLIndex di) noexcept
{
    assert(dense.size() == dense_info.size());
    assert(di < dense.size());

    const LLIndex old_prev = dense_info[di].prev;
    const LLIndex old_next = dense_info[di].next;
    const LLIndex si = static_cast<LLIndex>(dense[di]);

    sparse[si] = LLIndex_INVALID;

    if (old_prev != LLIndex_INVALID)
        dense_info[old_prev].next = old_next;

    if(old_next != LLIndex_INVALID)
        dense_info[old_next].prev = old_prev;

    if (patch_head == di)
        patch_head = old_next;

    dense[di] = Entity_INVALID;
    dense_info[di].next = free_head;

    assert(dense_count > 0);
    dense_count--;

    free_head = di;
}

void CompPool2Base::dense_patch(LLIndex di, GTick tick) noexcept
{
    assert(di < dense_info.size());

    if (patch_head != di)
    {
        const LLIndex old_prev = dense_info[di].prev;
        const LLIndex old_next = dense_info[di].next;

        if (old_prev != LLIndex_INVALID)
            dense_info[old_prev].next = old_next;

        if(old_next != LLIndex_INVALID)
            dense_info[old_next].prev = old_prev;

        if (patch_head != LLIndex_INVALID)
            dense_info[patch_head].prev = di;

        dense_info[di].next = patch_head;
        dense_info[di].prev = LLIndex_INVALID;
        patch_head = di;
    }

    dense_info[patch_head].tick = tick;
}
} // namespace zecs