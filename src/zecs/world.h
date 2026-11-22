#pragma once

#include "zecs/comp_pool.h"
#include "zecs/query_scope.h"
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

    // False for the root and for an entity that is not valid.
    // The entity is still valid in the handlers of on_removed that run here.
    // Contract: such a handler does not remove this entity again and does
    // not add components to it. It is checked by assert.
    bool entity_remove(Entity e);

    bool entity_is_valid(Entity e);

    // ------------------------------------------
    // Component API
    // ------------------------------------------
    // Contract: comp_add needs an entity that does not have this component yet.
    // It is checked by assert. Use comp_get_or_add when it may be there.

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
    void comp_patch(Entity e, T* comp); // same as comp_patch<T>(e): the pointer only tells the type
    void comp_patch(Entity e, GUid guid);

    // The signals of a component type. See the contract of their handlers
    // in comp_pool_base.h: a handler does not add or remove components
    // of this type and does not connect or disconnect its handlers.
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

    template<typename T>
    ViewEvents<T> view_events(const TickContext& ctx);

    // ------------------------------------------
    // Tick API
    // ------------------------------------------

    GTick tick_get();

    // ------------------------------------------
    // Query API
    // ------------------------------------------
    // Fast checks for several component types in a loop.
    // Make a scope one time before the loop: it finds the pools (and makes
    // the missing ones), so the calls with it do not search for them again.
    // Contract: a scope is used only with the world that made it.
    // It is checked by assert.

    template<typename... T>
    QueryScope<T...> query_scope();

    // True when the entity has every component of the scope.
    // The types are checked in their order, with a stop at the first one
    // that the entity does not have. So put the rarest component first.
    template<size_t N>
    bool query_has_all(Entity e, const QueryScopeBase<N>& scope) const noexcept;

    // True when the entity has at least one component of the scope.
    // It stops at the first one that the entity has: put the most common first.
    template<size_t N>
    bool query_has_any(Entity e, const QueryScopeBase<N>& scope) const noexcept;

    // Calls `body(entity, components...)` for every entity that has every
    // component of the scope. The components come by reference, in the order
    // of the types of the scope: there is no need to get them in the body.
    //   world.query_for_each(scope, [](Entity e, Position& p, Velocity& v) { ... });
    // The order of the entities is not fixed, and it is not the order of a view.
    // The body can add and remove components and entities. A component
    // that the body adds can be visited in the same pass or not.
    // Do not call compact() in the body.
    template<typename... T, typename F>
    void query_for_each(const QueryScope<T...>& scope, F&& body);

    // ------------------------------------------
    // Utility API
    // ------------------------------------------

    // Frees the memory left after removed components. Loops over views
    // get faster too: they have no empty slots to skip.
    // Call it after many components were removed, like at the end of a level.
    // Pointers to components are not valid after it, and the order of
    // entities in views changes. Do not call it inside a view loop.
    void compact();

    Counters counters_get() const;

private:
    friend struct ViewCompGroupIter;

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    template<typename F, typename... T, size_t... I>
    static void query_for_each_loop(F& body, std::index_sequence<I...>, const CompPool2Base& lead, CompPool2<T>&... pool);

    using CompPool2CreateFn = CompPool2Base* (*)();
    CompPool2Base* comp_pool_get_impl(CUidIndex index);
    CompPool2Base* comp_pool_add(CUidIndex index, CompPool2CreateFn create_fn);

    bool entity_is_removing(Entity e) const; // only for asserts

    using EventDequeCreateFn = EventDequeBase* (*)();
    EventDequeBase* event_pool_get_impl(EUidIndex index);
    EventDequeBase* event_pool_add(EUidIndex index, EventDequeCreateFn create_fn);

    Entities entities;
    size_t entity_count = 0;

    CompPools comp_pools;
    CompPoolsPacked comp_pools_packed;

    EventDeques event_deques;
    EventDequesPacked event_deques_packed;

    Entity entities_root = Entity_INVALID;
    // Index of the last removed entity, the head of the free list.
    // A removed slot of `entities` keeps the index of the next free slot
    // and the generation for the entity that will take this slot.
    EntityBase entity_free_index = Entity_INDEX_NONE;
    // Entities that entity_remove is removing now. The list is used only
    // when ZECS_DEBUG is defined. The field is always here, so the size
    // of World is the same in debug and release builds.
    std::vector<Entity> entities_removing;

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

inline bool World::entity_is_removing(Entity e) const
{
    for (Entity removing : entities_removing)
        if (removing == e)
            return true;

    return false;
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
    assert(entity_is_valid(e) && !entity_is_removing(e));
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
    assert(entity_is_valid(e) && !entity_is_removing(e));
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
    auto* pool = comp_pool_get_or_add<T>();
    assert(!pool->signal_is_running()); // see the contract of the signals
    return pool->on_added;
}

template<typename  T>
inline CompPool2Signal& World::comp_on_removed()
{
    auto* pool = comp_pool_get_or_add<T>();
    assert(!pool->signal_is_running()); // see the contract of the signals
    return pool->on_removed;
}

template<typename T>
inline CompPool2Signal& World::comp_on_patched()
{
    auto* pool = comp_pool_get_or_add<T>();
    assert(!pool->signal_is_running()); // see the contract of the signals
    return pool->on_patched;
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

template<typename T>
inline ViewEvents<T> World::view_events(const TickContext& ctx)
{
    return ViewEvents<T>(event_deque_get<T>(), ctx.since);
}

template<typename... T>
inline QueryScope<T...> World::query_scope()
{
    return QueryScope<T...>({ comp_pool_get_or_add<T>()... });
}

template<size_t N>
inline bool World::query_has_all(Entity e, const QueryScopeBase<N>& scope) const noexcept
{
    assert(scope.pools[0]->world == this);
    return scope.has_all(e);
}

template<size_t N>
inline bool World::query_has_any(Entity e, const QueryScopeBase<N>& scope) const noexcept
{
    assert(scope.pools[0]->world == this);
    return scope.has_any(e);
}

template<typename... T, typename F>
inline void World::query_for_each(const QueryScope<T...>& scope, F&& body)
{
    assert(scope.pools[0]->world == this);

    // The pass goes over the pool with the smallest number of dense slots,
    // so it is the shortest. We count dense slots, not components:
    // the pass visits empty slots too.
    const CompPool2Base* lead = scope.pools[0];
    for (const CompPool2Base* pool : scope.pools)
        if (pool->dense_size() < lead->dense_size())
            lead = pool;

    // The pools of the scope stand in the order of its types.
    [&]<size_t... I>(std::index_sequence<I...> indices)
    {
        query_for_each_loop(body, indices, *lead, *static_cast<CompPool2<T>*>(scope.pools[I])...);
    }(std::index_sequence_for<T...>{});
}

// The pools come by reference on purpose. The loop reads the arrays
// of the pools on every step, because the body can add components and
// the arrays can move. With references the compiler can take these reads
// out of the loop when the body does not change the pools. With the
// pointers from the scope it can not.
template<typename F, typename... T, size_t... I>
inline void World::query_for_each_loop(F& body, std::index_sequence<I...>, const CompPool2Base& lead, CompPool2<T>&... pool)
{
    // The pass goes from the last slot to the first one: it was measured
    // to be faster than from the first to the last. Slot 0 is never
    // used, it is the end. Components that the body adds to the end
    // of the pool are not visited in this pass.
    for (DIndex i = static_cast<DIndex>(lead.dense.size()) - 1; i != DIndex_NONE; --i)
    {
        const Entity e = lead.dense[i];
        if (e == Entity_INVALID)
            continue;

        // One search in a pool gives the place of the component, and
        // the same search tells that the entity has it. The lead pool
        // needs no search: its component is in the slot of this step.
        DIndex di[sizeof...(T)];
        if ((((di[I] = (&pool == &lead) ? i : pool.dense_index_get(e)) != DIndex_NONE) && ...))
            body(e, *pool.comp_slot(di[I])...);
    }
}

inline GTick World::tick_get()
{
    return tick;
}

} // namespace zecs
