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
    // Links of the patch list. An empty slot of a pool with stable pointers
    // keeps the next empty slot in `next`: this is the free list.
    struct PatchNode
    {
        DIndex next = DIndex_NONE;
        DIndex prev = DIndex_NONE;
    };

    using Dense = std::vector<Entity>;
    using Ticks = std::vector<GTick>;
    using Patches = std::vector<PatchNode>;
    using Sparse = std::vector<DIndex>;

    // Goes through the entities of one pool, from the last slot to the first.
    // Why from the end: a pool without stable pointers moves its last
    // component into the slot of a removed one (see stable_pointers in
    // config.h). A pass from the end has visited the last component already.
    // So the loop body can remove the current component, and the pass skips
    // no entity. A pass from the start would skip the moved component.
    // A component added during the pass may be visited or not: with
    // stable pointers it can take an empty slot that the pass didn't reach.
    struct EntityIt
    {
        // The end iterator. Slot 0 is never used for a component, so it is the end.
        inline EntityIt() noexcept = default;

        inline bool at_end() const noexcept { return index == DIndex_NONE; }
        inline bool operator==(const EntityIt& other) const noexcept { return index == other.index; }
        inline EntityIt& operator++() noexcept { index--; next_matched(); return *this; }
        inline Entity operator*() const noexcept { return entity; }

    private:
        friend struct CompPool2Base;
        template<typename... T>
        friend struct ViewCompGroup2;

        // Goes to the next slot, also when it is empty. Only for a group view:
        // it checks the entity of every slot anyway.
        inline void step() noexcept { index--; entity = dense->operator[](index); }

        inline EntityIt(const Dense* dense_, size_t last) noexcept
            : index(last)
            , dense(dense_)
        {
            next_matched();
        }

        inline void next_matched() noexcept
        {
            // The entity is checked first: most slots aren't empty, and then
            // this is the only check. Slot 0 is always empty, so it stops
            // the loop. A slot that the loop body made empty has no entity
            // too (see dense_rem), so the pool size isn't checked here.
            entity = dense->operator[](index);
            while (entity == Entity_INVALID && index != DIndex_NONE)
            {
                index--;
                entity = dense->operator[](index);
            }
        }

        // size_t, not DIndex. Measured: with a 32-bit index the loop
        // of a group view with many pools is about 20% slower.
        size_t index = DIndex_NONE;
        const Dense* dense = nullptr;
        // The entity of the current slot. operator* returns it and doesn't
        // read the slot again. Measured: view loops are up to 10% faster.
        Entity entity = Entity_INVALID;
    };

    // Goes through the patch list from new to old.
    // The loop body can add, remove or patch any component: the pass
    // skips no entity and visits no entity two times.
    // How it works: every new tick is bigger than all old ones, so no two
    // components of a pool have the same tick. If a slot has the same tick
    // as before, it has the same component in the same place of the list.
    // An empty slot has GTick_INVALID. No component has this tick.
    // A component that is added or patched inside the loop moves to the head
    // of the list, so this pass doesn't visit it; the next pass does.
    struct PatchIt
    {
        inline PatchIt() noexcept = default;

        inline PatchIt(GTick since_, const CompPool2Base* pool_) noexcept
            : since(since_)
            , pool(pool_)
        {
            move_to(pool->patch_head, pool->ticks[pool->patch_head]);
        }

        inline bool operator==(const PatchIt& other) const noexcept { return head == other.head; }
        inline Entity operator*() const noexcept { return pool->dense[head]; }
        inline GTick tick() const noexcept { return cur_tick; }

        inline PatchIt& operator++() noexcept
        {
            // The next node was saved before the loop body. If its slot still
            // has the same tick, the body didn't remove, patch or move it.
            DIndex n = next;
            GTick t = next_tick;
            if (pool->ticks[n] != t) [[unlikely]]
            {
                if (pool->ticks[head] == t)
                {
                    // The body removed the current node, and the next node
                    // was the last one: the pool moved it into this slot.
                    n = head;
                }
                else if (pool->ticks[head] == cur_tick)
                {
                    // The current node didn't change, so its link is correct.
                    n = pool->patches[head].next;
                }
                else
                {
                    // The body changed both nodes. Search from the head of
                    // the list for the first node older than the current one.
                    // The pass is slow if the body does it on every step.
                    n = pool->patch_head;
                    while (pool->ticks[n] >= cur_tick && n != DIndex_NONE)
                        n = pool->patches[n].next;
                }

                t = pool->ticks[n];
            }

            move_to(n, t);
            return *this;
        }

    private:
        // Moves to node `n` with tick `t`. Slot 0 is the end. It can be
        // read like a node: its tick is GTick_INVALID and its link is 0.
        inline void move_to(DIndex n, GTick t) noexcept
        {
            if (n == DIndex_NONE || t < since)
                n = DIndex_NONE;
            else
                cur_tick = t;

            // Save the next node now. The loop body can patch, move or
            // remove the current node, and then its link isn't correct.
            head = n;
            next = pool->patches[n].next;
            next_tick = pool->ticks[next];
        }

        GTick since = 0;
        GTick cur_tick = GTick_INVALID; // tick of the current node; starts above any real tick
        GTick next_tick = GTick_INVALID; // tick of `next`: shows later if the node is still in the slot
        const CompPool2Base* pool = nullptr;
        DIndex head = DIndex_NONE;
        DIndex next = DIndex_NONE; // link of the current node, saved before the loop body runs
    };

    ZECS_EXPORT CompPool2Base(GUid guid_, const std::type_info* type_, bool stable_pointers_);
    virtual ~CompPool2Base() = default;

    constexpr bool empty() const noexcept;
    constexpr size_t size() const noexcept;
    ZECS_HOT_INLINE constexpr bool contains(Entity e) const noexcept;

    // Takes one tick from `tick`, but only when the entity has the component.
    void patch(Entity e, GTick* tick) noexcept;
    GTick patch_get(Entity e) const noexcept;

    // Removes the component of the entity. False if the entity has none.
    // The code is the same for all component types, so it is in the .cpp.
    // Only the work with the component object is in CompPool2<T>
    // (see comp_slot_rem).
    ZECS_EXPORT bool remove(Entity e) noexcept;

    virtual void* raw_get(Entity e) const = 0;
    virtual void* raw_add(Entity e, GTick* tick) = 0;
    virtual std::pair<void*, bool> raw_get_or_add(Entity e, GTick* tick) = 0;

    // The last dense slot that a pass over the pool visits. A pass goes
    // from this slot to slot 1, so the number is also the length of a pass:
    // the components and the empty slots between them.
    constexpr DIndex dense_last() const noexcept;

    // Moves components from the end into the empty slots and frees the memory.
    // Pointers to moved components aren't valid after it, and the order
    // of entities in a pass changes. Don't call it inside a view loop.
    // Components that can't be moved stay in their slots.
    ZECS_EXPORT void compact() noexcept;

    inline EntityIt entities_begin() const noexcept { return EntityIt(&dense, dense_last()); }
    inline PatchIt patches_begin(GTick since) const noexcept { return PatchIt(since, this); }
    inline PatchIt patches_end() const noexcept { return PatchIt(); }
    inline bool patch_empty(GTick since) const noexcept { return (patch_head != DIndex_NONE) ? ticks[patch_head] < since : true; }

    // The component type of this pool. Set once by CompPool2<T>.
    const GUid guid;
    const std::type_info* const type;
    // See stable_pointers in config.h.
    const bool stable_pointers;

    // on_added is called after the component is made.
    // on_removed is called before the component is destroyed.
    // Contract for the handlers. It is checked by assert.
    //   - A handler must not add or remove components of this pool.
    //     It can change components of other types: for example,
    //     on_removed can remove the other components of the entity.
    //   - A handler must not connect or disconnect handlers of this pool.
    CompPool2Signal on_added;
    CompPool2Signal on_removed;
    CompPool2Signal on_patched;

    // True while a signal of this pool is calling its handlers.
    // Use it only in asserts: without ZECS_DEBUG it is always false.
    inline bool signal_is_running() const noexcept { return signal_depth != 0; }

protected:
    friend struct World;

    ZECS_HOT_INLINE constexpr DIndex sparse_index_get(Entity e) const noexcept;
    ZECS_HOT_INLINE constexpr DIndex sparse_cell_make(Entity e, DIndex di) const noexcept;
    ZECS_HOT_INLINE constexpr DIndex dense_index_get(Entity e) const noexcept;

    // Takes a slot, saves the tick and puts the slot at the head of the patch list.
    ZECS_EXPORT DIndex dense_add(Entity e, GTick tick) noexcept;
    // Removes the entity of slot `di`. `empty` is the slot that becomes
    // empty. It is `di`, or it is the last slot: then the last slot
    // moves into `di` first.
    void dense_rem(DIndex di, DIndex empty) noexcept;
    // Moves the entity, the tick and the patch links of a slot. Not the component.
    void dense_move(DIndex from, DIndex to) noexcept;
    // Takes the slot out of the patch list.
    void patch_rem(DIndex di) noexcept;
    ZECS_EXPORT void dense_patch(DIndex index, GTick tick) noexcept;
    ZECS_EXPORT void patched_emit(Entity e) noexcept;

    // Calls the handlers of a signal of this pool.
    inline void signal_emit(const CompPool2Signal& signal, Entity e) noexcept;

    // For remove(): destroy the component in slot `di`. If `from` is
    // another slot, move its component into `di` after that.
    virtual void comp_slot_rem(DIndex di, DIndex from) noexcept = 0;

    // For compact(): move one component (false if the type can't be moved)
    // and free the pages that `size` slots don't need.
    virtual bool comp_slot_move(DIndex from, DIndex to) noexcept = 0;
    virtual void comp_slots_trim(DIndex size) noexcept = 0;

    World* world = nullptr;

    Dense dense;
    Ticks ticks;
    Patches patches;
    size_t dense_count = 0;

    Sparse sparse;
    size_t sparse_size = 0;

    DIndex free_head = DIndex_NONE;
    DIndex patch_head = DIndex_NONE;

    // Number of signals of this pool that are calling handlers now.
    // It is counted only when ZECS_DEBUG is defined. The field is always
    // here, so the size of the pool is the same in debug and release builds.
    uint32_t signal_depth = 0;
};

inline void CompPool2Base::signal_emit(const CompPool2Signal& signal, Entity e) noexcept
{
#ifdef ZECS_DEBUG
    ++signal_depth;
#endif

    signal.Emit(*world, e);

#ifdef ZECS_DEBUG
    --signal_depth;
#endif
}

constexpr DIndex CompPool2Base::dense_last() const noexcept
{
    // Without stable pointers the components are in the slots from 1 to
    // dense_count. `dense` can be longer: the slots after them are empty
    // (see dense_rem).
    return static_cast<DIndex>(stable_pointers ? dense.size() - 1 : dense_count);
}

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

    // The loop over listeners isn't inline, so patch() stays small
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

constexpr DIndex CompPool2Base::sparse_cell_make(Entity e, DIndex di) const noexcept
{
    // Low bits: the dense index. High bits: the generation of the entity.
    return di | (static_cast<DIndex>(e) & Entity_GEN_MASK);
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
