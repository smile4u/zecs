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
        GTick tick = GTick_INVALID;
        LLIndex next = LLIndex_INVALID;
        LLIndex prev = LLIndex_INVALID;
    };

    using Dense = std::vector<Entity>;
    using DenseInfo = std::vector<Node>;
    using Sparse = std::vector<LLIndex>;

    struct EntityIt
    {
        inline EntityIt(LLIndex index_) noexcept
            : index(index_)
            , dense(nullptr)
            , dense_size(0)
        { }

        inline EntityIt(LLIndex index_, const Dense* dense_) noexcept
            : index(index_)
            , dense(dense_)
            , dense_size(static_cast<LLIndex>(dense_->size()))
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
        
        LLIndex index;
        LLIndex dense_size;
        const Dense* dense;
    };

    struct PatchIt
    {
        inline PatchIt(LLIndex head_) noexcept 
            : head(head_)
            , since(0)
            , dense(nullptr)
            , dense_info(nullptr)
        { }

        inline PatchIt(LLIndex head_, GTick since_, const Dense* dense_, const DenseInfo* dense_info_) noexcept 
            : head(head_)
            , since(since_)
            , dense(dense_)
            , dense_info(dense_info_)
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
            if (head != LLIndex_INVALID)
            {
                head = (*dense_info)[head].next;
                validate();
            }
        }

        inline void validate() noexcept
        {
            if (head != LLIndex_INVALID)
            {
                if ((*dense_info)[head].tick < since)
                    head = LLIndex_INVALID;
            }
        }

        GTick since;
        LLIndex head;
        const Dense* dense;
        const DenseInfo* dense_info;
    };
 
    CompPool2Base() = default;
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
    inline EntityIt entities_end() const noexcept { return EntityIt(static_cast<LLIndex>(dense.size())); }

    inline PatchIt patches_begin(GTick since) const noexcept { return PatchIt(patch_head, since, &dense, &dense_info); }
    inline PatchIt patches_end() const noexcept { return PatchIt(LLIndex_INVALID); }
    inline bool patch_empty(GTick since) const noexcept { return (patch_head != LLIndex_INVALID) ? dense_info[patch_head].tick < since : true; }

    CompPool2Signal on_added;
    CompPool2Signal on_removed;
    CompPool2Signal on_patched;

protected:
    friend struct World;

    constexpr LLIndex sparse_index_get(Entity e) const noexcept;
    constexpr LLIndex dense_index_get(Entity e) const noexcept;

    ZECS_EXPORT LLIndex dense_add(Entity e) noexcept;
    ZECS_EXPORT void dense_rem(LLIndex index) noexcept;
    ZECS_EXPORT void dense_patch(LLIndex index, GTick tick) noexcept;

    World* world = nullptr;

    Dense dense;
    DenseInfo dense_info;
    size_t dense_count = 0;
    size_t dense_size = 0;

    Sparse sparse;
    size_t sparse_size = 0;

    LLIndex free_head = LLIndex_INVALID;
    LLIndex patch_head = LLIndex_INVALID;
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
    const LLIndex di = dense_index_get(e);
    if (di == LLIndex_INVALID)
        return;

    dense_patch(di, tick);
    on_patched.Emit(*world, e);
}

inline GTick CompPool2Base::patch_get(Entity e) const noexcept
{
    const LLIndex di = dense_index_get(e);
    return (di != LLIndex_INVALID) ? dense_info[di].tick : GTick_INVALID;
}

constexpr bool CompPool2Base::contains(Entity e) const noexcept
{
    return (dense_index_get(e) != LLIndex_INVALID);
}

constexpr LLIndex CompPool2Base::sparse_index_get(Entity e) const noexcept
{ 
    return static_cast<LLIndex>(e);
}

constexpr LLIndex CompPool2Base::dense_index_get(Entity e) const noexcept
{ 
    const LLIndex si = sparse_index_get(e);
    return (si < sparse_size) ? sparse[si] : LLIndex_INVALID;
};

} // namespace zecs
