#include "zecs/world.h"
#include "zecs/comp_pool_base.h"
#include "zecs/guid.h"
#include "zecs/types.h"

#include <cassert>

namespace zecs
{

inline uint64_t world_unique_id_counter = 0;

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

    if (entity_free_index != EntityBase_INVALID)
    {
        assert(false); // TODO: implement
    }
    else
    {
        e = static_cast<Entity>(entities.size());
        entities.push_back(e);
    }

    counters.entities++;
    return e;
}

void World::entity_remove(Entity e)
{
    if (e == entities_root)
        return;

    if (!entity_is_valid(e))
        return;

    for (CompPool2Base* pool: comp_pools_packed)
        pool->raw_remove(e);

    size_t eindex = static_cast<size_t>(e);
    assert(eindex < entities.size());

    entities[eindex] = Entity_INVALID;
    counters.entities--;
}

void* World::comp_add(Entity e, GUid guid)
{
    CompPool2Base* pool = comp_pool_get(guid);
    if (!pool)
        return nullptr;

    return pool->raw_add(e, &tick);
}

void* World::comp_get_or_add(Entity e, GUid guid)
{
    CompPool2Base* pool = comp_pool_get(guid);
    if (!pool)
        return nullptr;

    auto res = pool->raw_get_or_add(e, &tick);
    return res.first;
}

CompPool2Base* World::comp_pool_add(CUidIndex cindex, CompPool2CreateFn create_fn)
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
