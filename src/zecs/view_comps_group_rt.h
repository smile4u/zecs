#pragma once

#include "zecs/comp_pool_base.h"
#include "zecs/view_comps_group_base.h"

#include <array>
#include <variant>

namespace zecs
{

// Runtime/debug queries own their storage and traversal independently of the
// hot-path typed view. Small groups keep their pool pointers inline.
struct ZECS_EXPORT ViewCompGroupRt : private ViewCompGroupBase
{
    using Pools = CompPoolsPacked;
    static constexpr size_t INLINE_CAPACITY = 4;

    struct It
    {
        using InternalIt = CompPool2Base::EntityIt;

        inline It() noexcept = default;
        inline It(InternalIt it_, const ViewCompGroupRt* view_) noexcept : it(it_), view(view_) { next_matched(); }
        inline It& operator++() noexcept { ++it; next_matched(); return *this; }
        inline bool operator==(const It& other) const noexcept { return it == other.it; }
        inline Entity operator*() const noexcept { return *it; }

    private:
        InternalIt it;
        const ViewCompGroupRt* view = nullptr;

        void next_matched() noexcept;
    };

    ViewCompGroupRt(Pools pools_);
    ViewCompGroupRt(World& world, const GUid* guids, size_t count_);

    inline bool empty() const noexcept { return begin() == end(); }
    It begin() const noexcept;
    // Each begin iterator owns its traversal bound, including during mutation.
    inline It end() const noexcept { return It(); }

    inline bool has(Entity e) const noexcept
    {
        const auto* ptr = pool_data();
        if (count == 0 || ptr[0] == nullptr)
            return false;

        for (size_t i = 0; i < count; ++i) { if (!ptr[i]->contains(e)) return false; }
        return true;
    }

private:
    using InlinePools = std::array<CompPool2Base*, INLINE_CAPACITY>;

    CompPool2Base** pool_data() noexcept;
    CompPool2Base* const* pool_data() const noexcept;
    std::variant<InlinePools, Pools> pools{InlinePools{}};
    size_t count = 0;
};

} // namespace zecs
