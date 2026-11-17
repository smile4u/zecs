#pragma once

#include "zecs/comp_pool.h"
#include "zecs/comp_tuple.h"
#include "zecs/guid.h"
#include "zecs/events.h"
#include "zecs/types.h"
#include "zecs/view_comps.h"
#include "zecs/view_comps_group.h"
#include "zecs/view_comps_group_rt.h"
#include "zecs/view_entities.h"
#include "zecs/view_events.h"
#include "zecs/view_patched_comps.h"
#include "zecs/view_pools.h"

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace zecs
{

struct ZECS_EXPORT World
{
    struct Counters
    {
        size_t entities = 0;
        size_t components = 0;
        size_t pools = 0;
    };

    World(const char* world_name = nullptr);
    ~World();

    const char* name_get() const;
    void name_set(const char* world_name);

    // ------------------------------------------
    // Entity API
    // ------------------------------------------
    // Contract: comp_add and comp_get_or_add need a valid entity.
    // It is checked by assert. Other functions take any ID: for a removed
    // entity they do nothing and return nullptr or false.

    Entity root();

    Entity entity_add();
    bool entity_remove(Entity e); // false for the root and for an entity that is not valid

    bool entity_is_valid(Entity e);

    // ------------------------------------------
    // Component API
    // ------------------------------------------

    template<typename T, typename... Args>
    T* comp_add(Entity e, Args&&... args);
    void* comp_add(Entity e, GUid guid);

    template<typename T, typename... Args>
    T* comp_add_sc(Args&&... args);

    template<typename T>
    T* comp_get(Entity e);
    void* comp_get(Entity e, GUid guid);

    template<typename T>
    T* comp_get_sc();

    template<typename T>
    T* comp_get_or_add(Entity e);
    void* comp_get_or_add(Entity e, GUid guid);

    template<typename T>
    T* comp_get_or_add_sc();

    template<typename T>
    bool comp_remove(Entity e);
    bool comp_remove(Entity e, GUid guid);

    template<typename T>
    bool comp_remove_sc();

    void comp_remove_all(Entity e);

    template<typename T>
    void comp_patch(Entity e);

    template<typename T>
    void comp_patch(Entity e, T* comp);
    void comp_patch(Entity e, GUid guid);

    template<typename T>
    CompPool2Signal& comp_on_added();

    template<typename T>
    CompPool2Signal& comp_on_removed();

    template<typename T>
    CompPool2Signal& comp_on_patched();

    // ------------------------------------------
    // Pools API
    // ------------------------------------------

    template<typename T>
    CompPool2<T>* comp_pool_get();
    CompPool2Base* comp_pool_get(GUid guid);

    template<typename T>
    CompPool2<T>* comp_pool_get_or_add();

    // ------------------------------------------
    // Events API
    // DRAFT: not finished and not reviewed yet. See known_issues.md.
    // ------------------------------------------

    template<typename T>
    EventDeque<T>* event_deque_get();
    EventDequeBase* event_deque_get(GUid guid);

    template<typename T>
    EventDeque<T>* event_deque_get_or_add();

    template<typename T>
    void event_push(T&& event);

    template<typename T, typename... Args>
    void event_push(Args&&... args);

    // ------------------------------------------
    // Views API
    // NOTE-1: Views are non-copyable and non-movable.
    // NOTE-2: Keep a view alive while using its iterators.
    // ------------------------------------------

    ViewPools view_pools();
    ViewEntities view_entities();

    template<typename T>
    ViewComps view_comps();
    ViewComps view_comps(GUid guid);

    template<typename... T>
    ViewCompGroup2<T...> view_comp_group();
    ViewCompGroupRt view_comp_group(const GUid* guids, size_t count);

    template<typename T>
    ViewPatchedComps view_patched_comps(const TickContext& ctx);
    ViewPatchedComps view_patched_comps(GUid guid, const TickContext& ctx);

#if 0    
    template<typename T, typename... OtherComps>
    ViewPatchedGroup<T, OtherComps...> view_patched_group(const TickContext& ctx);
    ViewPatchedGroupRt view_patched_group(const TickContext& ctx, GUid patched_guid, const GUid* others_guids, size_t others_count);
#endif

    template<typename T>
    ViewEvents<T> view_events(const TickContext& ctx);

    // ------------------------------------------
    // Tick API
    // ------------------------------------------

    GTick tick_get();

    // ------------------------------------------
    // Utility API
    // ------------------------------------------

    template<typename... T>
    CompTupl<T...> comps_tuple_get();

    template<typename... T>
    bool has_all(Entity e, const CompTupl<T...>& comp_tuple);

    template<typename... T>
    bool has_any(Entity e, const CompTupl<T...>& comp_tuple);

    const Counters& counters_get();

private:
    friend struct ViewCompGroupIter;

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    using CompPool2CreateFn = CompPool2Base* (*)();
    CompPool2Base* comp_pool_get_impl(CUidIndex index);
    CompPool2Base* comp_pool_add(CUidIndex index, CompPool2CreateFn create_fn);

    using EventDequeCreateFn = EventDequeBase* (*)();
    EventDequeBase* event_pool_get_impl(EUidIndex index);
    EventDequeBase* event_pool_add(EUidIndex index, EventDequeCreateFn create_fn);

    Entities entities;
    Counters counters;

    CompPools comp_pools;
    CompPoolsPacked comp_pools_packed;

    EventDeques event_deques;
    EventDequesPacked event_deques_packed;

    Entity entities_root = Entity_INVALID;
    // Index of the last removed entity, the head of the free list.
    // A removed slot of `entities` keeps the index of the next free slot
    // and the generation for the entity that will take this slot.
    EntityBase entity_free_index = Entity_INDEX_NONE;

    std::string name;
    GTick tick = 0;
};

inline const char* World::name_get() const
{
    return name.c_str();
}

inline void World::name_set(const char* world_name)
{
    name = (world_name != nullptr) ? std::string(world_name) : std::string();
}

inline Entity World::root()
{
    return entities_root;
}

inline bool World::entity_is_valid(Entity e)
{
    // A removed slot never keeps its own index, so it is not equal to any ID.
    // Entity_INVALID has an index that is never in use.
    size_t eindex = entity_to_index(e);
    return (eindex < entities.size()) && (entities[eindex] == e);
}

inline bool World::comp_remove(Entity e, GUid guid)
{
    CompPool2Base* pool = comp_pool_get(guid);
    return (pool) ? pool->raw_remove(e) : false;
}

inline void World::comp_patch(Entity e, GUid guid)
{
    CompPool2Base* pool = comp_pool_get(guid);
    if (pool)
        return pool->patch(e, &tick);
}

inline void* World::comp_get(Entity e, GUid guid)
{
    CompPool2Base* pool = comp_pool_get(guid);
    if (pool)
        return pool->raw_get(e);

    return nullptr;
}

template<typename T>
inline CompPool2<T>* World::comp_pool_get()
{
    const CUidIndex cindex = cuidIndex<T>;    
    return static_cast<CompPool2<T>*>(comp_pool_get_impl(cindex));
}

inline CompPool2Base* World::comp_pool_get(GUid guid)
{
    const CUidIndex cindex = cuid_index_get(guid);
    return comp_pool_get_impl(cindex);
}

inline CompPool2Base* World::comp_pool_get_impl(CUidIndex cindex)
{
    const size_t index = static_cast<size_t>(cindex);
    return (index < comp_pools.size()) ? comp_pools[index].get() : nullptr;
}

template<typename T>
CompPool2<T>* World::comp_pool_get_or_add()
{
    const CUidIndex cindex = cuidIndex<T>;
    CompPool2Base* ret = comp_pool_get_impl(cindex);
    if (!ret)
    {
        ret = comp_pool_add(cindex, []() -> CompPool2Base* { return new CompPool2<T>(); });
    }

    return static_cast<CompPool2<T>*>(ret);
}

template<typename T, typename... Args>
inline T* World::comp_add(Entity e, Args&&... args)
{
    assert(entity_is_valid(e));
    return comp_pool_get_or_add<T>()->add(e, &tick, std::forward<Args>(args)...);
}

template<typename T, typename... Args>
inline T* World::comp_add_sc(Args&&... args)
{
    return comp_add<T>(entities_root, std::forward<Args>(args)...);
}

template<typename T>
inline T* World::comp_get(Entity e)
{
    auto* pool = comp_pool_get<T>();
    return (pool) ? pool->get(e) : nullptr;
}

template<typename T>
inline T* World::comp_get_sc()
{
    return comp_get<T>(entities_root);
}

template<typename T>
inline T* World::comp_get_or_add(Entity e)
{
    assert(entity_is_valid(e));
    auto res = comp_pool_get_or_add<T>()->get_or_add(e, &tick);
    return res.first;
}

template<typename T>
inline T* World::comp_get_or_add_sc()
{
    return comp_get_or_add<T>(entities_root);
}

template<typename T>
inline bool World::comp_remove(Entity e)
{
    auto* pool = comp_pool_get<T>();
    return (pool) ? pool->remove(e) : false;
}

template<typename T>
inline bool World::comp_remove_sc()
{
    return comp_remove<T>(entities_root);
}

template<typename T>
inline void World::comp_patch(Entity e)
{
    auto* pool = comp_pool_get<T>();
    if (!pool) return;
    pool->patch(e, &tick);
}

template<typename T>
inline void World::comp_patch(Entity e, T* comp)
{
    auto* pool = comp_pool_get<T>();
    if (!pool) return;
    pool->patch(e, &tick);
}

template<typename  T>
inline CompPool2Signal& World::comp_on_added()
{
    return comp_pool_get_or_add<T>()->on_added;
}

template<typename  T>
inline CompPool2Signal& World::comp_on_removed()
{
    return comp_pool_get_or_add<T>()->on_removed;
}

template<typename T>
inline CompPool2Signal& World::comp_on_patched()
{
    return comp_pool_get_or_add<T>()->on_patched;
}

template<typename T>
inline EventDeque<T>* World::event_deque_get()
{
    const EUidIndex sparce_index = euidIndex<T>;
    return static_cast<EventDeque<T>*>(event_pool_get_impl(sparce_index));
}

inline EventDequeBase* World::event_deque_get(GUid guid)
{
    const EUidIndex eindex = euid_index_get(guid);
    return event_pool_get_impl(eindex);
}

inline EventDequeBase* World::event_pool_get_impl(EUidIndex eindex)
{
    const size_t index = static_cast<size_t>(eindex);
    return (index < event_deques.size()) ? event_deques[index].get() : nullptr;
}

template<typename T>
inline EventDeque<T>* World::event_deque_get_or_add()
{
    const EUidIndex eindex = euidIndex<T>;
    EventDequeBase* ret = event_pool_get_impl(eindex);
    if (!ret)
    {
        static EventDequeCreateFn create_fn = []() -> EventDequeBase* { return new EventDeque<T>(); };
        ret = event_pool_add(eindex, create_fn);
    }

    return static_cast<EventDeque<T>*>(ret);
}

template<typename T>
inline void World::event_push(T&& event)
{
    event_deque_get_or_add<T>()->push(std::move(event), tick++);
}

template<typename T, typename... Args>
inline void World::event_push(Args&&... args)
{
    event_deque_get_or_add<T>()->push(T(std::forward<Args>(args)...));
}

inline ViewEntities World::view_entities()
{
    return ViewEntities(entities);
}

inline ViewPools World::view_pools()
{
    return ViewPools(comp_pools_packed);
}

template<typename T>
inline ViewComps World::view_comps()
{
    auto* pool = comp_pool_get<T>();
    return ViewComps(pool);
}

inline ViewComps World::view_comps(GUid guid)
{
    CompPool2Base* pool = comp_pool_get(guid);
    return ViewComps(pool);
}

template<typename... T>
inline ViewCompGroup2<T...> World::view_comp_group()
{
    return ViewCompGroup2<T...>({ comp_pool_get<T>()... });
}

inline ViewCompGroupRt World::view_comp_group(const GUid* guids, size_t count)
{
    return ViewCompGroupRt(*this, guids, count);
}

template<typename T>
inline ViewPatchedComps World::view_patched_comps(const TickContext& ctx)
{
    return ViewPatchedComps(comp_pool_get<T>(), ctx.since);
}

inline ViewPatchedComps World::view_patched_comps(GUid guid, const TickContext& ctx)
{
    CompPool2Base* pool = comp_pool_get(guid);
    return ViewPatchedComps(pool, ctx.since);
}

#if 0
template<typename T, typename ...OtherComps>
inline ViewPatchedGroup<T, OtherComps...> World::view_patched_group(const TickContext& ctx)
{
    return ViewPatchedGroup<T, OtherComps...>(*this, ctx);
}

inline ViewPatchedGroupRt World::view_patched_group(const TickContext& ctx, GUid patched_guid, const GUid* others_guids, size_t others_count)
{
    return ViewPatchedGroupRt(*this, ctx, patched_guid, others_guids, others_count);;
}
#endif

template<typename T>
inline ViewEvents<T> World::view_events(const TickContext& ctx)
{
    return ViewEvents<T>(event_deque_get<T>(), ctx.since);
}

template<typename... T>
inline CompTupl<T...> World::comps_tuple_get()
{
    return CompTupl<T...>(comp_pool_get<T>()...);
}

template<typename ...T>
inline bool World::has_all(Entity e, const CompTupl<T...>& comp_tuple)
{
    return comp_tuple.has_all(e);
}

template<typename ...T>
inline bool World::has_any(Entity e, const CompTupl<T...>& comp_tuple)
{
    return comp_tuple.has_any(e);
}

inline GTick World::tick_get()
{
    return tick;
}

} // namespace zecs
