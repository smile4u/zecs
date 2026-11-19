#pragma once

namespace zecs
{

struct ViewCompGroupBase
{
protected:
    ViewCompGroupBase() = default;
    ViewCompGroupBase(const ViewCompGroupBase&) = delete;
    ViewCompGroupBase(ViewCompGroupBase&&) = delete;
    ViewCompGroupBase& operator=(const ViewCompGroupBase&) = delete;
    ViewCompGroupBase& operator=(ViewCompGroupBase&&) = delete;
};

} // namespace zecs
