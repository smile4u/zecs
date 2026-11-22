#pragma once

#include "zecs/world.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace test
{

struct Position
{
    int x = 0;
    int y = 0;
};

struct Velocity
{
    int dx = 0;
    int dy = 0;
};

struct Tag
{
};

struct DamageEvent
{
    int amount = 0;
};

struct NonDefault
{
    explicit NonDefault(int value_)
        : value(value_)
    {
    }

    int value = 0;
};

struct TrackedLifetime
{
    TrackedLifetime()
    {
        ++alive;
        ++constructed;
    }

    explicit TrackedLifetime(int value_)
        : value(value_)
    {
        ++alive;
        ++constructed;
    }

    TrackedLifetime(const TrackedLifetime& other)
        : value(other.value)
    {
        ++alive;
        ++constructed;
    }

    TrackedLifetime(TrackedLifetime&& other) noexcept
        : value(other.value)
    {
        ++alive;
        ++constructed;
    }

    ~TrackedLifetime()
    {
        --alive;
        ++destroyed;
    }

    static void reset() noexcept
    {
        alive = 0;
        constructed = 0;
        destroyed = 0;
    }

    int value = 0;

    inline static int alive = 0;
    inline static int constructed = 0;
    inline static int destroyed = 0;
};

template<typename View>
std::vector<zecs::Entity> collect_entities(View&& view)
{
    std::vector<zecs::Entity> entities;
    for (zecs::Entity entity : view)
        entities.push_back(entity);

    return entities;
}

// The same entities in a fixed order. Use it when a test checks which
// entities a pass visits: the order of a pass isn't a part of the API.
inline std::vector<zecs::Entity> sorted(std::vector<zecs::Entity> entities)
{
    std::sort(entities.begin(), entities.end());
    return entities;
}

inline std::vector<zecs::CompPool2Base*> collect_pools(zecs::ViewPools view)
{
    std::vector<zecs::CompPool2Base*> pools;
    for (zecs::CompPool2Base* pool : view)
        pools.push_back(pool);

    return pools;
}

struct EntityWorldFixture
{
    EntityWorldFixture()
        : world("main")
    {
        first = world.entity_add();
        second = world.entity_add();
        third = world.entity_add();
    }

    zecs::World world;
    zecs::Entity first = zecs::Entity_INVALID;
    zecs::Entity second = zecs::Entity_INVALID;
    zecs::Entity third = zecs::Entity_INVALID;
};

struct ComponentWorldFixture
{
    ComponentWorldFixture()
        : world("components")
    {
        position_entity = world.entity_add();
        get_or_add_entity = world.entity_add();
        tag_entity = world.entity_add();
        removable_entity = world.entity_add();
        tracked_first = world.entity_add();
        tracked_second = world.entity_add();

        world.comp_add<Position>(position_entity, 10, 20);
        world.comp_add<Tag>(tag_entity);
        world.comp_add<NonDefault>(tag_entity, 42);

        world.comp_add<Position>(removable_entity, 1, 2);
        world.comp_add<Velocity>(removable_entity, 3, 4);

        world.comp_add<TrackedLifetime>(tracked_first, 1);
        world.comp_add<TrackedLifetime>(tracked_second, 2);

        world.comp_add_sc<Velocity>(3, 5);
    }

    zecs::World world;
    zecs::Entity position_entity = zecs::Entity_INVALID;
    zecs::Entity get_or_add_entity = zecs::Entity_INVALID;
    zecs::Entity tag_entity = zecs::Entity_INVALID;
    zecs::Entity removable_entity = zecs::Entity_INVALID;
    zecs::Entity tracked_first = zecs::Entity_INVALID;
    zecs::Entity tracked_second = zecs::Entity_INVALID;
};

struct ViewWorldFixture
{
    ViewWorldFixture()
        : world("views")
    {
        position_only = world.entity_add();
        in_group_a = world.entity_add();
        in_group_b = world.entity_add();
        velocity_only = world.entity_add();
        tag_only = world.entity_add();
        patch_first = world.entity_add();
        patch_second = world.entity_add();

        world.comp_add<Position>(position_only, 1, 1);

        world.comp_add<Position>(in_group_a, 2, 2);
        world.comp_add<Velocity>(in_group_a, 20, 20);

        world.comp_add<Position>(in_group_b, 3, 3);
        world.comp_add<Velocity>(in_group_b, 30, 30);

        world.comp_add<Velocity>(velocity_only, 4, 4);
        world.comp_add<Tag>(tag_only);

        world.comp_add<Position>(patch_first, 5, 5);
        world.comp_add<Position>(patch_second, 6, 6);

        tick_after_setup = world.tick_get();
    }

    zecs::TickContext since_setup() const
    {
        return zecs::TickContext(tick_after_setup);
    }

    zecs::World world;
    zecs::Entity position_only = zecs::Entity_INVALID;
    zecs::Entity in_group_a = zecs::Entity_INVALID;
    zecs::Entity in_group_b = zecs::Entity_INVALID;
    zecs::Entity velocity_only = zecs::Entity_INVALID;
    zecs::Entity tag_only = zecs::Entity_INVALID;
    zecs::Entity patch_first = zecs::Entity_INVALID;
    zecs::Entity patch_second = zecs::Entity_INVALID;
    zecs::GTick tick_after_setup = 0;
};

} // namespace test
