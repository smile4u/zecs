#pragma once

#include "zecs/exports.h"
#include "zecs/guid.h"
#include "zecs/types.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <signal/Signal.h>

namespace zecs
{
using CompPool2Signal = Gallant::Signal2<World&, Entity>;

struct CompPool2Base
{
    struct CompNode
    {
        // Live slot: comp points at the component (nullptr only for the guard slot
        // and transiently inside remove()). Dead slot: free_next is the next
        // free-list index, DIndex_NONE ends the list. Liveness is tracked by
        // dense[di] != Entity_INVALID, so the active member is always known from
        // context and no type-punning happens.
        union
        {
            void* comp;
            DIndex free_next;
        };
        GTick tick = GTick_INVALID;

        CompNode() noexcept : comp(nullptr) {}
    };

    // Patch-list linkage, kept out of `CompNode` so the hot `get()`
    // path indexes a 16-byte (power-of-two) stride instead of 24 bytes.
    // The free-list does NOT use these patches: it is threaded through
    // CompNode::free_next of dead slots, so patches[] of a dead slot stays a stable
    // waypoint for in-flight PatchIt iterators (see dense_rem).
    struct PatchNode
    {
        DIndex next = DIndex_NONE;
        DIndex prev = DIndex_NONE;
    };

    using Dense = std::vector<Entity>;
    using Comps = std::vector<CompNode>;
    using Patches = std::vector<PatchNode>;
    using Sparse = std::vector<DIndex>;

    struct EntityIt
    {
        inline EntityIt(DIndex index_) noexcept
            : index(index_)
            , dense(nullptr)
            , dense_size(0)
        { }

        inline EntityIt(DIndex index_, const Dense* dense_) noexcept
            : index(index_)
            , dense(dense_)
            , dense_size(static_cast<DIndex>(dense_->size()))
        {
            next_matched();
        }

        inline bool operator==(const EntityIt& other) const noexcept { return index == other.index; }
        inline bool operator!=(const EntityIt& other) const noexcept { return index != other.index; }
        inline EntityIt& operator++() noexcept { index++; next_matched(); return *this; }
        inline Entity operator*() const noexcept { return dense->operator[](index); }

    private:
        inline void next_matched() noexcept
        {
            while (index < dense_size && dense->operator[](index) == Entity_INVALID) index++;
        }

        DIndex index;
        DIndex dense_size;
        const Dense* dense;
    };

    struct PatchIt
    {
        inline PatchIt(DIndex head_) noexcept
            : head(head_)
            , since(0)
            , dense(nullptr)
            , comps(nullptr)
            , patches(nullptr)
        { }

        inline PatchIt(DIndex head_, GTick since_, const Dense* dense_, const Comps* comps_, const Patches* patches_) noexcept
            : head(head_)
            , since(since_)
            , dense(dense_)
            , comps(comps_)
            , patches(patches_)
        {
            validate();
        }

        inline bool operator==(const PatchIt& other) const noexcept { return head == other.head; }
        inline bool operator!=(const PatchIt& other) const noexcept { return head != other.head; }
        inline PatchIt& operator++() noexcept { next_matched(); return *this; }
        inline Entity operator*() const noexcept { return (*dense)[head]; }
        inline GTick tick() const noexcept { return (*comps)[head].tick; }

    private:
        inline void next_matched() noexcept
        {
            if (head != DIndex_NONE)
            {
                head = (*patches)[head].next;
                validate();
            }
        }

        inline void validate() noexcept
        {
            while (head != DIndex_NONE)
            {
                // Skip dead slots: their patches[] keep the stale patch-list
                // linkage (see dense_rem), so a removed node's successor chain
                // stays walkable instead of yielding Entity_INVALID.
                if ((*dense)[head] == Entity_INVALID)
                {
                    head = (*patches)[head].next;
                    continue;
                }

                if ((*comps)[head].tick < since)
                    head = DIndex_NONE;

                break;
            }
        }

        GTick since;
        DIndex head;
        const Dense* dense;
        const Comps* comps;
        const Patches* patches;
    };

    ZECS_EXPORT CompPool2Base();
    virtual ~CompPool2Base() = default;

    constexpr bool empty() const noexcept;
    constexpr size_t size() const noexcept;
    constexpr bool contains(Entity e) const noexcept;

    void patch(Entity e, GTick tick) noexcept;
    GTick patch_get(Entity e) const noexcept;

    virtual GUid guid() const = 0;
    virtual const std::type_info* type() const = 0;
    virtual void* raw_get(Entity e) const = 0;
    virtual void* raw_add(Entity e, GTick tick) = 0;
    virtual std::pair<void*, bool> raw_get_or_add(Entity e, GTick* tick) = 0;
    virtual bool raw_remove(Entity e) = 0;

    inline EntityIt entities_begin() const noexcept { return EntityIt(0, &dense); }
    inline EntityIt entities_end() const noexcept { return EntityIt(static_cast<DIndex>(dense.size())); }

    inline PatchIt patches_begin(GTick since) const noexcept { return PatchIt(patch_head, since, &dense, &comps, &patches); }
    inline PatchIt patches_end() const noexcept { return PatchIt(DIndex_NONE); }
    inline bool patch_empty(GTick since) const noexcept { return (patch_head != DIndex_NONE) ? comps[patch_head].tick < since : true; }

    CompPool2Signal on_added;
    CompPool2Signal on_removed;
    CompPool2Signal on_patched;

protected:
    friend struct World;

    constexpr DIndex sparse_index_get(Entity e) const noexcept;
    constexpr DIndex dense_index_get(Entity e) const noexcept;

    ZECS_EXPORT DIndex dense_add(Entity e) noexcept;
    ZECS_EXPORT void dense_rem(DIndex index) noexcept;
    ZECS_EXPORT void dense_patch(DIndex index, GTick tick) noexcept;

    World* world = nullptr;

    // dense/comps slot 0 is a permanent guard: dense[0] == Entity_INVALID
    // and comps[0].comp == nullptr forever, never handed out by dense_add()
    // and never pushed onto the free-list. This lets sparse/free_head/patch_head/
    // PatchNode::next/prev use plain 0 as their "no value" sentinel, and lets
    // get()/patch() index comps[di] unconditionally without a validity branch.
    // Note: a dead slot's CompNode holds its free-list link in free_next (see
    // dense_rem), so comp == nullptr means "no component" only for the guard
    // and for slots reached via sparse[].
    Dense dense;
    Comps comps;
    Patches patches;
    size_t dense_count = 0;
    size_t dense_size = 0;

    Sparse sparse;
    size_t sparse_size = 0;

    DIndex free_head = DIndex_NONE;
    DIndex patch_head = DIndex_NONE;
};

constexpr bool CompPool2Base::empty() const noexcept
{
    return dense_count == 0;
}

constexpr size_t CompPool2Base::size() const noexcept
{
    return dense_count;
}

inline void CompPool2Base::patch(Entity e, GTick tick) noexcept
{
    const DIndex di = dense_index_get(e);
    if (!comps[di].comp)
        return;

    dense_patch(di, tick);
    on_patched.Emit(*world, e);
}

inline GTick CompPool2Base::patch_get(Entity e) const noexcept
{
    const DIndex di = dense_index_get(e);
    return comps[di].tick;
}

constexpr bool CompPool2Base::contains(Entity e) const noexcept
{
    return (dense_index_get(e) != DIndex_NONE);
}

constexpr DIndex CompPool2Base::sparse_index_get(Entity e) const noexcept
{
    return static_cast<DIndex>(e);
}

constexpr DIndex CompPool2Base::dense_index_get(Entity e) const noexcept
{
    const DIndex si = sparse_index_get(e);
    return (si < sparse_size) ? sparse[si] : DIndex_NONE;
};

} // namespace zecs
