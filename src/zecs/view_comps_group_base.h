#pragma once

#include "zecs/exports.h"

#include <cstddef>

namespace zecs
{

struct CompPool2Base;

struct ViewCompGroupBase
{
protected:
    // Requires at least two pools. Put the smallest pool, or a missing pool,
    // in slot 0; each view owns its storage and traversal independently.
    static ZECS_EXPORT void select_lead(CompPool2Base** pools, size_t count) noexcept;
};

} // namespace zecs
