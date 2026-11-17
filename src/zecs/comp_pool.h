#pragma once

#include "zecs/comp_pool_base.h"
#include "zecs/mem_pool.h"
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

    // raw_add() gives no arguments. Then only a type with
    // a default constructor can be made.
    template<typename... Args>
    static constexpr bool can_add = std::is_default_constructible_v<T> || sizeof...(Args) > 0;

    ~CompPool2()
    {
        if constexpr (!std::is_empty_v<T>)
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

    const std::type_info* type() const override
    {
        return &typeid(T);
    }

    GUid guid() const override
    {
        return globalUid<T>;
    }

    void* raw_get(Entity e) const override
    {
        return get(e);
    }

    void* raw_add(Entity e, GTick tick) override
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
        // Fast path: no `if` here. When the entity has no component, di is 0.
        // Slot 0 of dense/comps is never used for a real component and its
        // comp is always nullptr, so this line returns nullptr by itself.
        const DIndex di = dense_index_get(e);
        return static_cast<T*>(comps[di].comp);
    }

    template<typename... Args>
    T* add(Entity e, GTick tick, Args&&... args) noexcept
    {
        assert(e != Entity_INVALID);

        if constexpr (can_add<Args...>)
        {
            const DIndex di = dense_add(e);

            T* comp;
            if constexpr (std::is_empty_v<T>)
            {
                comp = reinterpret_cast<T*>(this);
            }
            else
            {
                comp = ::new (comp_slot_ensure(di)) T(std::forward<Args>(args)...);
            }

            dense[di] = e;
            comps[di].comp = comp;
            dense_patch(di, tick);

            on_added.Emit(*world, e);
            return comp;
        }
        else
        {
            return nullptr;
        }
    }

    template<typename... Args>
    std::pair<T*, bool> get_or_add(Entity e, GTick* tick, Args&&... args) noexcept
    {
        T* comp = get(e);
        if (comp)
            return std::make_pair(comp, false);

        if constexpr (can_add<Args...>)
            return std::make_pair(add(e, (*tick)++, std::forward<Args>(args)...), true);
        else
            return std::make_pair(nullptr, false);
    }

    inline bool remove(Entity e) noexcept
    {
        const DIndex di = dense_index_get(e);
        if (di == DIndex_NONE)
            return false;

        on_removed.Emit(*world, e);

        if constexpr (!std::is_empty_v<T>)
            comp_slot(di)->~T();

        comps[di].comp = nullptr;

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
    // the data lives in di-order parallel to dense/comps instead of being
    // pointer-chased through a separate allocator.
    static constexpr uint32_t PAGE_BITS = std::popcount(PAGE_SIZE - 1);
    static constexpr uint32_t PAGE_MASK = PAGE_SIZE - 1;

    // Returns the slot for an existing dense index. The page is guaranteed to
    // exist because comp_slot_ensure() is called for every live di on add().
    inline T* comp_slot(DIndex di) const noexcept
    {
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