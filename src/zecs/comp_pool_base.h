#pragma once

#include "zecs/exports.h"
#include "zecs/guid.h"
#include "zecs/types.h"

#include <cstddef>
#include <typeinfo>
#include <vector>

#include <signal/Signal.h>

namespace zecs
{
using CompPool2Signal = Gallant::Signal2<World&, Entity>;

template<typename... T>
struct ViewCompGroup2;

struct CompPool2Base
{
    // Links of the patch list. The free list does not use these links
    // (it uses `ticks`). So a removed slot keeps its old links, and
    // a PatchIt that stands on it can still move on (see dense_rem).
    struct PatchNode
    {
        DIndex next = DIndex_NONE;
        DIndex prev = DIndex_NONE;
    };

    using Dense = std::vector<Entity>;
    using Ticks = std::vector<GTick>;
    using Patches = std::vector<PatchNode>;
    using Sparse = std::vector<DIndex>;

    // Goes through the entities of one pool.
    // The end is saved when the pass starts, also when end() is called again.
    // A component added during the pass may be visited or not: it can
    // take a free slot before the end. There is no guarantee.
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
            // here: both sides are plain bool. Example: (a & b) | c.
            // It is not safe when the right side needs the left one: p && p->x.
            //
            // Written in this form for MSVC: against end() `b` is a known true,
            // and the whole line becomes just `a`. The shorter form
            // (a == b) & (a | same index) was left by MSVC as 7 instructions.
            const bool a = at_end();
            const bool b = other.at_end();
            return (a & b) | (!a & !b & (index == other.index));
        }

        inline EntityIt& operator++() noexcept { index++; next_matched(); return *this; }
        inline Entity operator*() const noexcept { return dense->operator[](index); }

    private:
        friend struct CompPool2Base;
        template<typename... T>
        friend struct ViewCompGroup2;

        // Goes to the next slot, also when it is empty. Only for a group view:
        // it checks the entity of every slot anyway.
        inline void step() noexcept { index++; }

        inline EntityIt(const Dense* dense_) noexcept
            : index(DIndex_FIRST)
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
    // A component that is added or patched inside the loop moves to the head
    // of the list, so this pass does not visit it; the next pass does.
    // A body that patches the next node on every step makes the pass slow
    // (see the search from the head in move_to).
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
        inline PatchIt& operator++() noexcept { move_to(next); return *this; }
        inline Entity operator*() const noexcept { return pool->dense[head]; }
        inline GTick tick() const noexcept { return pool->ticks[head]; }

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
                const GTick t = pool->ticks[n];
                if (t >= cur_tick) [[unlikely]]
                {
                    n = pool->patch_head;
                    while (n != DIndex_NONE && pool->ticks[n] >= cur_tick)
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
        GTick cur_tick = GTick_INVALID; // tick of the current node; starts above any real tick
        const CompPool2Base* pool = nullptr;
        DIndex head = DIndex_NONE;
        DIndex next = DIndex_NONE; // link of the current node, saved before the loop body runs
    };

    ZECS_EXPORT CompPool2Base(GUid guid_, const std::type_info* type_);
    virtual ~CompPool2Base() = default;

    constexpr bool empty() const noexcept;
    constexpr size_t size() const noexcept;
    ZECS_HOT_INLINE constexpr bool contains(Entity e) const noexcept;

    // Takes one tick from `tick`, but only when the entity has the component.
    void patch(Entity e, GTick* tick) noexcept;
    GTick patch_get(Entity e) const noexcept;

    virtual void* raw_get(Entity e) const = 0;
    virtual void* raw_add(Entity e, GTick* tick) = 0;
    virtual std::pair<void*, bool> raw_get_or_add(Entity e, GTick* tick) = 0;
    virtual bool raw_remove(Entity e) = 0;

    inline EntityIt entities_begin() const noexcept { return EntityIt(&dense); }

    inline PatchIt patches_begin(GTick since) const noexcept { return PatchIt(since, this); }
    inline PatchIt patches_end() const noexcept { return PatchIt(); }
    inline bool patch_empty(GTick since) const noexcept { return (patch_head != DIndex_NONE) ? ticks[patch_head] < since : true; }

    // The component type of this pool. Set once by CompPool2<T>.
    const GUid guid;
    const std::type_info* const type;

    CompPool2Signal on_added;
    CompPool2Signal on_removed;
    CompPool2Signal on_patched;

protected:
    friend struct World;

    ZECS_HOT_INLINE constexpr DIndex sparse_index_get(Entity e) const noexcept;
    ZECS_HOT_INLINE constexpr DIndex dense_index_get(Entity e) const noexcept;

    // Takes a slot, saves the tick and puts the slot at the head of the patch list.
    ZECS_EXPORT DIndex dense_add(Entity e, GTick tick) noexcept;
    ZECS_EXPORT void dense_rem(DIndex index) noexcept;
    ZECS_EXPORT void dense_patch(DIndex index, GTick tick) noexcept;
    ZECS_EXPORT void patched_emit(Entity e) noexcept;

    World* world = nullptr;

    Dense dense;
    Ticks ticks;
    Patches patches;
    size_t dense_count = 0;

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

inline void CompPool2Base::patch(Entity e, GTick* tick) noexcept
{
    const DIndex di = dense_index_get(e);
    if (di == DIndex_NONE)
        return;

    dense_patch(di, (*tick)++);

    // The loop over listeners is not inline, so patch() stays small
    // and the compiler puts it inside loops.
    if (!on_patched.Empty())
        patched_emit(e);
}

inline GTick CompPool2Base::patch_get(Entity e) const noexcept
{
    const DIndex di = dense_index_get(e);
    return ticks[di];
}

constexpr bool CompPool2Base::contains(Entity e) const noexcept
{
    return (dense_index_get(e) != DIndex_NONE);
}

constexpr DIndex CompPool2Base::sparse_index_get(Entity e) const noexcept
{
    return entity_to_index(e);
}

constexpr DIndex CompPool2Base::dense_index_get(Entity e) const noexcept
{
    const DIndex si = sparse_index_get(e);
    if (si >= sparse_size)
        return DIndex_NONE;

    // sparse[] keeps the dense index together with the generation of the entity.
    // The same generation in the ID makes the high bits 0, and the dense index is left.
    // An old ID gives a number that is too big. An empty cell gives 0 or a big number.
    const DIndex di = sparse[si] ^ (static_cast<DIndex>(e) & Entity_GEN_MASK);
    return (di <= Entity_INDEX_MASK) ? di : DIndex_NONE;
}

} // namespace zecs
