#pragma once

#include "zecs/comp_pool_base.h"
#include "zecs/config.h"
#include "zecs/mem_pool.h"
#include "zecs/traits.h"
#include "zecs/types.h"

#include <bit>
#include <cassert>
#include <type_traits>
#include <utility>
#include <vector>

namespace zecs
{

template<typename T>
struct CompPool2 : public CompPool2Base
{
    static constexpr uint32_t PAGE_SIZE = CompConfig<T>::page_size;
    static_assert(std::has_single_bit(PAGE_SIZE));

    CompPool2() : CompPool2Base(globalUid<T>, &typeid(T)) {}

    ~CompPool2()
    {
        if constexpr (!is_tag_component<T>)
        {
            for (DIndex di = 0; di < dense.size(); ++di)
            {
                if (dense[di] != Entity_INVALID)
                    comp_slot(di)->~T();
            }

            for (T* page : pages)
                mem_pool_data_free(page, alignof(T));
        }
    }

    void* raw_get(Entity e) const override
    {
        return get(e);
    }

    void* raw_add(Entity e, GTick* tick) override
    {
        return add(e, tick);
    }

    std::pair<void*, bool> raw_get_or_add(Entity e, GTick* tick) override
    {
        return get_or_add(e, tick);
    }

    bool raw_remove(Entity e) override
    {
        return remove(e);
    }

    inline T* get(Entity e) const noexcept
    {
        // The address is computed from di. A pointer stored for every slot
        // would be one more read from a big array, and that read often
        // misses the cache. `pages` is small and stays in cache.
        const DIndex di = dense_index_get(e);
        return (di != DIndex_NONE) ? comp_slot(di) : nullptr;
    }

    // Takes one tick from `tick`, but only when the component is added.
    template<typename... Args>
    T* add(Entity e, GTick* tick, Args&&... args) noexcept
    {
        assert(e != Entity_INVALID);

        if constexpr (std::is_default_constructible_v<T> || sizeof...(Args) > 0)
        {
            const DIndex di = dense_add(e, (*tick)++);

            T* comp;
            if constexpr (is_tag_component<T>)
            {
                static_assert(std::is_trivially_constructible_v<T, Args...>,
                    "a tag is made without a constructor call, so it takes no arguments");

                comp = comp_slot(di);
            }
            else
            {
                comp = ::new (comp_slot_ensure(di)) T(std::forward<Args>(args)...);
            }

            // The handlers do not add or remove components of this pool,
            // so the pointer is still correct after them.
            signal_emit(on_added, e);
            return comp;
        }
        else
        {
            // raw_add() gives no arguments, and this type
            // has no default constructor.
            return nullptr;
        }
    }

    template<typename... Args>
    std::pair<T*, bool> get_or_add(Entity e, GTick* tick, Args&&... args) noexcept
    {
        T* comp = get(e);
        if (comp)
            return std::make_pair(comp, false);

        comp = add(e, tick, std::forward<Args>(args)...);
        return std::make_pair(comp, comp != nullptr);
    }

    inline bool remove(Entity e) noexcept
    {
        assert(!signal_is_running()); // see the contract of the signals

        const DIndex di = dense_index_get(e);
        if (di == DIndex_NONE)
            return false;

        // The handlers do not add or remove components of this pool,
        // so `di` is still correct after them.
        signal_emit(on_removed, e);

        if constexpr (!is_tag_component<T>)
            comp_slot(di)->~T();

        dense_rem(di);
        return true;
    }

private:
    friend struct World;

    bool comp_slot_move(DIndex from, DIndex to) noexcept override
    {
        if constexpr (is_tag_component<T>)
        {
            return true;
        }
        else if constexpr (std::is_move_constructible_v<T>)
        {
            ::new (comp_slot(to)) T(std::move(*comp_slot(from)));
            comp_slot(from)->~T();
            return true;
        }
        else
        {
            return false;
        }
    }

    void comp_slots_trim(DIndex size) noexcept override
    {
        // Slot 0 has no component, so an empty pool needs no page.
        const size_t page_count = (size > DIndex_FIRST) ? ((size - 1) >> PAGE_BITS) + 1 : 0;
        while (pages.size() > page_count)
        {
            mem_pool_data_free(pages.back(), alignof(T));
            pages.pop_back();
        }
    }

    // Component storage is inline and indexed by the dense index (di), so that
    // the data lives in di-order parallel to dense instead of being
    // pointer-chased through a separate allocator.
    static constexpr uint32_t PAGE_BITS = std::countr_zero(PAGE_SIZE);
    static constexpr uint32_t PAGE_MASK = PAGE_SIZE - 1;

    // Returns the slot for an existing dense index. The page is guaranteed to
    // exist because comp_slot_ensure() is called for every live di on add().
    inline T* comp_slot(DIndex di) const noexcept
    {
        // A tag has no object: the pointer only says "the tag is here".
        if constexpr (is_tag_component<T>)
            return reinterpret_cast<T*>(const_cast<CompPool2*>(this));
        else
            return &pages[di >> PAGE_BITS][di & PAGE_MASK];
    }

    // Allocates the page covering di if needed and returns the raw slot.
    inline void* comp_slot_ensure(DIndex di) noexcept
    {
        const size_t pi = di >> PAGE_BITS;
        while (pages.size() <= pi)
            pages.push_back(static_cast<T*>(mem_pool_data_alloc(sizeof(T) * PAGE_SIZE, alignof(T))));

        return &pages[pi][di & PAGE_MASK];
    }

    std::vector<T*> pages;
};

} // namespace zecs