#pragma once

#include "zecs/comp_pool_base.h"
#include "zecs/types.h"

namespace zecs
{
struct ViewPatchedComps
{
    using It = CompPoolBase::PatchIt;

    ViewPatchedComps(const ViewPatchedComps&) = delete;
    ViewPatchedComps(ViewPatchedComps&&) = delete;
    ViewPatchedComps& operator=(const ViewPatchedComps&) = delete;
    ViewPatchedComps& operator=(ViewPatchedComps&&) = delete;

    inline ViewPatchedComps(CompPoolBase* pool_, GTick since_) : since(since_), pool(pool_) { }
    inline bool empty() const { return (pool) ? pool->patch_empty(since) : true; }
    inline It begin() { return (pool) ? pool->patches_begin(since) : It(); }
    inline It end() { return (pool) ? pool->patches_end() : It(); }

protected:
    GTick since;
    CompPoolBase* pool;
};
} //namespace zecs
