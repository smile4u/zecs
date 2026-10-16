#pragma once

#include "zecs/exports.h"

#include <cstddef>

namespace zecs
{

struct CompPool2Base;

struct ViewCompGroupBase
{
protected:
    ViewCompGroupBase() = default;
    ViewCompGroupBase(const ViewCompGroupBase&) = delete;
    ViewCompGroupBase(ViewCompGroupBase&&) = delete;
    ViewCompGroupBase& operator=(const ViewCompGroupBase&) = delete;
    ViewCompGroupBase& operator=(ViewCompGroupBase&&) = delete;

    // Requires at least two pools. Put the smallest pool, or a missing pool,
    // in slot 0; each view owns its storage and traversal independently.
    static ZECS_EXPORT void select_lead(CompPool2Base** pools, size_t count) noexcept;
};

} // namespace zecs
