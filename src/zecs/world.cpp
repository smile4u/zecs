#include "zecs/world.h"
#include "zecs/comp_pool_base.h"
#include "zecs/guid.h"
#include "zecs/types.h"

#include <cassert>

namespace zecs
{

World::World(const char* world_name)
{
    entities_root = entity_add();
    name_set(world_name);
}

World::~World()
{
}

Entity World::entity_add()
{
    Entity e = Entity_INVALID;

    if (entity_free_index != Entity_INDEX_NONE)
    {
        // A removed slot keeps the next free index and the generation for this entity.
        const EntityBase eindex = entity_free_index;
        const EntityBase removed = static_cast<EntityBase>(entities[eindex]);

        entity_free_index = removed & Entity_INDEX_MASK;
        e = static_cast<Entity>((removed & Entity_GEN_MASK) | eindex);
        entities[eindex] = e;
    }
    else
    {
        // No free index is left, see the limits in types.h.
        if (entities.size() >= Entity_INDEX_NONE) [[unlikely]]
            return Entity_INVALID;

        e = static_cast<Entity>(entities.size());
        entities.push_back(e);
    }

    entity_count++;
    return e;
}

bool World::entity_remove(Entity e)
{
    if (e == entities_root)
        return false;

    if (!entity_is_valid(e))
        return false;

    // The entity stays valid until all its components are removed, so
    // handlers of on_removed can use it. They must not remove it again.
    assert(!entity_is_removing(e));
#ifdef ZECS_DEBUG
    entities_removing.push_back(e);
#endif

    comp_remove_all(e);

#ifdef ZECS_DEBUG
    // A handler can remove other entities. Each of them leaves the list
    // before the handler returns, so this entity is the last one here.
    assert(entities_removing.back() == e);
    entities_removing.pop_back();
#endif

    const EntityBase eindex = entity_to_index(e);
    const EntityBase gen = static_cast<EntityBase>(e) & Entity_GEN_MASK;
    entity_count--;

    // This was the last generation of the index. The next one would be 0
    // again, and a very old ID would be equal to the new entity. So the
    // index doesn't go to the free list: it is never used again.
    // Entity_INVALID in the slot isn't equal to any ID.
    if (gen == Entity_GEN_MASK) [[unlikely]]
    {
        entities[eindex] = Entity_INVALID;
        return true;
    }

    // The generation is in the high bits, so this adds 1 to it.
    const EntityBase next_gen = gen + (Entity_INDEX_MASK + 1);

    entities[eindex] = static_cast<Entity>(next_gen | entity_free_index);
    entity_free_index = eindex;
    return true;
}

void World::comp_remove_all(Entity e)
{
    // There is no check of the entity: no pool contains an ID that isn't valid.
    // A handler of on_removed can make a new pool. Then the list of pools
    // grows and can move to new memory. So we read the list again after
    // every removal. New pools are checked too.
    CompPoolBase** pools = comp_pools_packed.data();
    size_t count = comp_pools_packed.size();
    for (size_t i = 0; i < count; ++i)
    {
        // Most pools don't have this entity. The check
        // is inline, so we don't pay for a call.
        if (!pools[i]->contains(e))
            continue;

        pools[i]->remove(e);
        pools = comp_pools_packed.data();
        count = comp_pools_packed.size();
    }
}

World::Counters World::counters_get() const
{
    // Components and pools are counted here, on request: a counter
    // inside add() and remove() would slow down every call.
    Counters counters;
    counters.entities = entity_count;
    counters.pools = comp_pools_packed.size();

    for (const CompPoolBase* pool : comp_pools_packed)
        counters.components += pool->size();

    return counters;
}

void* World::comp_add(Entity e, GUid guid)
{
    assert(entity_is_valid(e) && !entity_is_removing(e));
    CompPoolBase* pool = comp_pool_get(guid);
    if (!pool)
        return nullptr;

    return pool->raw_add(e, &tick);
}

void* World::comp_get_or_add(Entity e, GUid guid)
{
    assert(entity_is_valid(e) && !entity_is_removing(e));
    CompPoolBase* pool = comp_pool_get(guid);
    if (!pool)
        return nullptr;

    auto res = pool->raw_get_or_add(e, &tick);
    return res.first;
}

void World::compact()
{
    for (CompPoolBase* pool : comp_pools_packed)
        pool->compact();
}

CompPoolBase* World::comp_pool_add(CUidIndex cindex, CompPoolCreateFn create_fn)
{
    const size_t index = static_cast<size_t>(cindex);
    if (index >= comp_pools.size())
        comp_pools.resize(index + 1);

    assert(comp_pools[index].get() == nullptr);
    comp_pools[index].reset((*create_fn)());
    comp_pools[index]->world = this;

    comp_pools_packed.push_back(comp_pools[index].get());
    return comp_pools[index].get();
}

EventDequeBase* World::event_pool_add(EUidIndex eindex, EventDequeCreateFn create_fn)
{
    const size_t index = static_cast<size_t>(eindex);
    if (index >= event_deques.size())
        event_deques.resize(index + 1);

    assert(event_deques[index].get() == nullptr);
    event_deques[index].reset((*create_fn)());

    event_deques_packed.push_back(event_deques[index].get());
    return event_deques[index].get();
}

} // namespace zecs
