#pragma once

#include "zecs/exports.h"
#include "zecs/guid.h"
#include "zecs/types.h"

#include <cstddef>
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

    // Goes through the entities of one pool.
    // The end is saved when the pass starts: slots added during the pass
    // are not visited, also when end() is called again.
    struct EntityIt
    {
        // The end iterator.
        inline EntityIt() noexcept = default;

        // `>=` is the opposite of the check in next_matched(),
        // so the compiler joins the two checks into one.
        inline bool at_end() const noexcept { return index >= dense_size; }

        // Two end iterators are always equal, also from different passes.
        inline bool operator==(const EntityIt& other) const noexcept
        {
            // `&` and `|` always compute both sides, so there are no jumps
            // like with `&&` and `||`. It is faster in the loop. It is safe
            // here: both sides are plain bool. Example: (a == b) & (c | d).
            // It is not safe when the right side needs the left one: p && p->x.
            return (at_end() == other.at_end()) & (at_end() | (index == other.index));
        }

        inline bool operator!=(const EntityIt& other) const noexcept { return !(*this == other); }
        inline EntityIt& operator++() noexcept { index++; next_matched(); return *this; }
        inline Entity operator*() const noexcept { return dense->operator[](index); }

    private:
        friend struct CompPool2Base;

        // Slot 0 is a permanent guard, so the first real slot is 1.
        inline EntityIt(const Dense* dense_) noexcept
            : index(1)
            , dense(dense_)
            , dense_size(static_cast<DIndex>(dense_->size()))
        {
            next_matched();
        }

        inline void next_matched() noexcept
        {
            while (index < dense_size && dense->operator[](index) == Entity_INVALID) index++;
        }

        // The pointer is between the two numbers on purpose. Side by side they
        // are packed into one register, and that makes the loop about 2x slower.
        DIndex index = 0;
        const Dense* dense = nullptr;
        DIndex dense_size = 0;
    };

    // Goes through the patch list from new to old.
    // It is safe to add, remove or patch components inside the loop.
    // Rule: every new patch tick is bigger than all old ones.
    struct PatchIt
    {
        inline PatchIt() noexcept = default;

        inline PatchIt(GTick since_, const CompPool2Base* pool_) noexcept
            : since(since_)
            , pool(pool_)
        {
            move_to(pool->patch_head);
        }

        inline bool operator==(const PatchIt& other) const noexcept { return head == other.head; }
        inline bool operator!=(const PatchIt& other) const noexcept { return head != other.head; }
        inline PatchIt& operator++() noexcept { move_to(next); return *this; }
        inline Entity operator*() const noexcept { return pool->dense[head]; }
        inline GTick tick() const noexcept { return pool->comps[head].tick; }

    private:
        // Moves to node `n`, or to the first good node after it.
        inline void move_to(DIndex n) noexcept
        {
            while (n != DIndex_NONE)
            {
                // Removed slot: skip it. Its old link still works (see dense_rem).
                if (pool->dense[n] == Entity_INVALID)
                {
                    n = pool->patches[n].next;
                    continue;
                }

                // The next node must be older than the current one. If it is not,
                // the loop body patched it or reused its slot, and its link goes
                // back to the start of the list. Search from the head instead.
                const GTick t = pool->comps[n].tick;
                if (t >= cur_tick) [[unlikely]]
                {
                    n = pool->patch_head;
                    while (n != DIndex_NONE && pool->comps[n].tick >= cur_tick)
                        n = pool->patches[n].next;

                    continue;
                }

                if (t < since)
                    n = DIndex_NONE;
                else
                    cur_tick = t;

                break;
            }

            // Save the next link now: the loop body can patch or replace
            // the current node, and then its link is not correct anymore.
            head = n;
            next = pool->patches[n].next;
        }

        GTick since = 0;
        GTick cur_tick = GTick_INVALID; // tick of the current node
        const CompPool2Base* pool = nullptr;
        DIndex head = DIndex_NONE;
        DIndex next = DIndex_NONE; // link of the current node, saved before the loop body runs
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

    inline EntityIt entities_begin() const noexcept { return EntityIt(&dense); }

    inline PatchIt patches_begin(GTick since) const noexcept { return PatchIt(since, this); }
    inline PatchIt patches_end() const noexcept { return PatchIt(); }
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
