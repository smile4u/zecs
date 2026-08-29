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
    struct Node
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

        Node() noexcept : comp(nullptr) {}
    };

    // Patch-list linkage, kept out of `Node` so the hot `get()`
    // path indexes a 16-byte (power-of-two) stride instead of 24 bytes.
    // The free-list does NOT use these links: it is threaded through
    // Node::free_next of dead slots, so links[] of a dead slot stays a stable
    // waypoint for in-flight PatchIt iterators (see dense_rem).
    struct Link
    {
        DIndex next = DIndex_NONE;
        DIndex prev = DIndex_NONE;
    };

    using Dense = std::vector<Entity>;
    using DenseInfo = std::vector<Node>;
    using Links = std::vector<Link>;
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
            , dense_info(nullptr)
            , links(nullptr)
        { }

        inline PatchIt(DIndex head_, GTick since_, const Dense* dense_, const DenseInfo* dense_info_, const Links* links_) noexcept
            : head(head_)
            , since(since_)
            , dense(dense_)
            , dense_info(dense_info_)
            , links(links_)
        {
            validate();
        }

        inline bool operator==(const PatchIt& other) const noexcept { return head == other.head; }
        inline bool operator!=(const PatchIt& other) const noexcept { return head != other.head; }
        inline PatchIt& operator++() noexcept { next_matched(); return *this; }
        inline Entity operator*() const noexcept { return (*dense)[head]; }
        inline GTick tick() const noexcept { return (*dense_info)[head].tick; }

    private:
        inline void next_matched() noexcept
        {
            if (head != DIndex_NONE)
            {
                head = (*links)[head].next;
                validate();
            }
        }

        inline void validate() noexcept
        {
            while (head != DIndex_NONE)
            {
                // Skip dead slots: their links[] keep the stale patch-list
                // linkage (see dense_rem), so a removed node's successor chain
                // stays walkable instead of yielding Entity_INVALID.
                if ((*dense)[head] == Entity_INVALID)
                {
                    head = (*links)[head].next;
                    continue;
                }

                if ((*dense_info)[head].tick < since)
                    head = DIndex_NONE;

                break;
            }
        }

        GTick since;
        DIndex head;
        const Dense* dense;
        const DenseInfo* dense_info;
        const Links* links;
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

    inline PatchIt patches_begin(GTick since) const noexcept { return PatchIt(patch_head, since, &dense, &dense_info, &links); }
    inline PatchIt patches_end() const noexcept { return PatchIt(DIndex_NONE); }
    inline bool patch_empty(GTick since) const noexcept { return (patch_head != DIndex_NONE) ? dense_info[patch_head].tick < since : true; }

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

    // dense/dense_info slot 0 is a permanent guard: dense[0] == Entity_INVALID
    // and dense_info[0].comp == nullptr forever, never handed out by dense_add()
    // and never pushed onto the free-list. This lets sparse/free_head/patch_head/
    // Link::next/prev use plain 0 as their "no value" sentinel, and lets
    // get()/patch() index dense_info[di] unconditionally without a validity branch.
    // Note: a dead slot's Node holds its free-list link in free_next (see
    // dense_rem), so comp == nullptr means "no component" only for the guard
    // and for slots reached via sparse[].
    Dense dense;
    DenseInfo dense_info;
    Links links;
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
    if (!dense_info[di].comp)
        return;

    dense_patch(di, tick);
    on_patched.Emit(*world, e);
}

inline GTick CompPool2Base::patch_get(Entity e) const noexcept
{
    const DIndex di = dense_index_get(e);
    return dense_info[di].tick;
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
