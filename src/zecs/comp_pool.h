#pragma once

#include "zecs/comp_pool_base.h"
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

template<typename T, typename = void>
struct CompPool2PageSize : std::integral_constant<std::size_t, 4096> {};

template<typename T>
struct CompPool2PageSize<T, std::void_t<decltype(T::page_size)>> : std::integral_constant<std::size_t, T::page_size> {};

template<typename T>
struct CompPool2 : public CompPool2Base
{
    static constexpr uint32_t PAGE_SIZE = CompPool2PageSize<T>::value;
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

            on_added.Emit(*world, e);
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
        const DIndex di = dense_index_get(e);
        if (di == DIndex_NONE)
            return false;

        on_removed.Emit(*world, e);

        if constexpr (!is_tag_component<T>)
            comp_slot(di)->~T();

        dense_rem(di);
        return true;
    }

    /*
    bool clone(Entity e, const T* src, GTick tick)
    {
        if constexpr (std::is_copy_assignable<T>::value)
        {
            auto it = comps.find(e);
            if (it == comps.end())
            {
                T* dst = add(e, tick);
                *dst = *src;
            }
            else
            {
                T* dst = static_cast<T*>(it->second.ptr.get());
                *dst = *src;
                patch(e, tick);
            }

            return true;
        }
        
        return false;
    }
    */

private:
    // Component storage is inline and indexed by the dense index (di), so that
    // the data lives in di-order parallel to dense instead of being
    // pointer-chased through a separate allocator.
    static constexpr uint32_t PAGE_BITS = std::popcount(PAGE_SIZE - 1);
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