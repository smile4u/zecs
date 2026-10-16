#pragma once

#include "zecs/comp_pool_base.h"
#include "zecs/types.h"

namespace zecs
{
struct ViewPatchedComps
{
    using It = CompPool2Base::PatchIt;

    ViewPatchedComps(const ViewPatchedComps&) = delete;
    ViewPatchedComps(ViewPatchedComps&&) = delete;
    ViewPatchedComps& operator=(const ViewPatchedComps&) = delete;
    ViewPatchedComps& operator=(ViewPatchedComps&&) = delete;

    inline ViewPatchedComps(CompPool2Base* pool_, GTick since_) :  pool(pool_), since(since_) { }
    inline bool empty() const { return (pool) ? pool->patch_empty(since) : true; }
    inline It begin() { return (pool) ? pool->patches_begin(since) : It(DIndex_NONE); }
    inline It end() { return (pool) ? pool->patches_end() : It(DIndex_NONE); }

protected:
    GTick since;
    CompPool2Base* pool;
};
} //namespace zecs
