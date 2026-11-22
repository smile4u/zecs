#include "doctest/doctest.h"

#include "test_common_api.h"

#include <vector>

namespace
{

// The default: the pool keeps no empty slots.
struct Packed
{
    int value = 0;
};

// Asks for pointers that stay valid, so its pool keeps empty slots.
struct Stable
{
    static constexpr bool stable_pointers = true;

    int value = 0;
};

// Removes a component of another type from the handler.
struct RemovePositionProbe
{
    void on_signal(zecs::World& world, zecs::Entity entity)
    {
        ++calls;
        world.comp_remove<test::Position>(entity);
    }

    int calls = 0;
};

// Makes `count` entities with the component. The value of a component
// is the number of its entity in the list.
template<typename T>
std::vector<zecs::Entity> fill(zecs::World& world, int count)
{
    std::vector<zecs::Entity> entities;
    for (int i = 0; i < count; ++i)
    {
        entities.push_back(world.entity_add());
        world.comp_add<T>(entities.back())->value = i;
    }

    return entities;
}

// True when a pass over the pool visits more slots than it has components.
template<typename T>
bool has_empty_slots(zecs::World& world)
{
    const auto* pool = world.comp_pool_get<T>();
    return pool->dense_last() != pool->size();
}

// True when no entity stands in the list two times.
bool all_different(std::vector<zecs::Entity> entities)
{
    entities = test::sorted(std::move(entities));
    return std::adjacent_find(entities.begin(), entities.end()) == entities.end();
}

} // namespace

// A pool without stable pointers has no empty slots.
TEST_CASE("packed pool")
{
    zecs::World world;

    SUBCASE("a removal moves the last component into the free slot")
    {
        const auto entities = fill<Packed>(world, 5);
        const Packed* second = world.comp_get<Packed>(entities[1]);

        CHECK(world.comp_remove<Packed>(entities[1]));

        CHECK(world.comp_get<Packed>(entities[1]) == nullptr);
        CHECK(world.comp_get<Packed>(entities[4]) == second);
        for (int i : { 0, 2, 3, 4 })
            CHECK(world.comp_get<Packed>(entities[i])->value == i);

        CHECK(test::sorted(test::collect_entities(world.view_comps<Packed>()))
            == test::sorted({ entities[0], entities[2], entities[3], entities[4] }));
    }

    SUBCASE("the last and the only component can be removed")
    {
        const auto entities = fill<Packed>(world, 3);

        CHECK(world.comp_remove<Packed>(entities[2]));
        CHECK(world.comp_remove<Packed>(entities[0]));
        CHECK(world.comp_remove<Packed>(entities[1]));

        CHECK(world.view_comps<Packed>().empty());

        world.comp_add<Packed>(entities[0])->value = 7;
        CHECK(world.comp_get<Packed>(entities[0])->value == 7);
    }

    SUBCASE("a tag pool is packed too")
    {
        const zecs::Entity first = world.entity_add();
        const zecs::Entity second = world.entity_add();
        const zecs::Entity third = world.entity_add();
        world.comp_add<test::Tag>(first);
        world.comp_add<test::Tag>(second);
        world.comp_add<test::Tag>(third);

        CHECK(world.comp_remove<test::Tag>(first));

        CHECK(test::sorted(test::collect_entities(world.view_comps<test::Tag>())) == test::sorted({ second, third }));
    }

    SUBCASE("a removed entity leaves no empty slots")
    {
        const auto entities = fill<Packed>(world, 6);
        for (zecs::Entity e : entities)
            world.comp_add<test::Position>(e);

        world.entity_remove(entities[0]);
        world.entity_remove(entities[3]);

        for (int i : { 1, 2, 4, 5 })
            CHECK(world.comp_get<Packed>(entities[i])->value == i);
    }

    SUBCASE("a pass that removes every component visits each entity one time")
    {
        const auto entities = fill<Packed>(world, 50);

        std::vector<zecs::Entity> visited;
        for (zecs::Entity e : world.view_comps<Packed>())
        {
            visited.push_back(e);
            CHECK(world.comp_remove<Packed>(e));
        }

        CHECK(test::sorted(visited) == test::sorted(entities));
        CHECK(world.view_comps<Packed>().empty());
    }

    SUBCASE("a pass can remove the components of visited entities")
    {
        const auto entities = fill<Packed>(world, 60);

        // Every third step removes the current component and two components
        // of entities that are visited already. So the pool gets shorter
        // by more than one slot in one step.
        std::vector<zecs::Entity> visited;
        std::vector<zecs::Entity> removed;
        for (zecs::Entity e : world.view_comps<Packed>())
        {
            visited.push_back(e);

            const size_t step = visited.size();
            if (step % 3 == 0)
            {
                for (zecs::Entity target : { e, visited[step - 2], visited[step - 3] })
                {
                    if (world.comp_remove<Packed>(target))
                        removed.push_back(target);
                }
            }
        }

        CHECK(all_different(visited));
        CHECK(test::sorted(visited) == test::sorted(entities));
        CHECK(world.comp_pool_get<Packed>()->size() == entities.size() - removed.size());

        // The components that are left keep their values.
        for (size_t i = 0; i < entities.size(); ++i)
        {
            if (const Packed* comp = world.comp_get<Packed>(entities[i]))
                CHECK(comp->value == static_cast<int>(i));
        }

        CHECK(test::collect_entities(world.view_comps<Packed>()).size() == entities.size() - removed.size());
    }

    SUBCASE("a pass can add components after it removed some")
    {
        const auto entities = fill<Packed>(world, 20);

        // A new component takes the first free slot. After a removal that slot
        // can be before the current one, so the pass can visit the new entity.
        std::vector<zecs::Entity> visited;
        std::vector<zecs::Entity> added;
        for (zecs::Entity e : world.view_comps<Packed>())
        {
            visited.push_back(e);
            REQUIRE(visited.size() <= 100);

            if (visited.size() <= 5)
            {
                CHECK(world.comp_remove<Packed>(e));
                added.push_back(world.entity_add());
                world.comp_add<Packed>(added.back())->value = 1000;
            }
        }

        // Every old entity one time; a new one not more than one time.
        CHECK(all_different(visited));
        for (zecs::Entity e : entities)
            CHECK(std::find(visited.begin(), visited.end(), e) != visited.end());

        CHECK(world.comp_pool_get<Packed>()->size() == entities.size());
    }

    SUBCASE("a group view can remove the components of the current entity")
    {
        const auto entities = fill<Packed>(world, 30);
        for (zecs::Entity e : entities)
            world.comp_add<test::Position>(e);

        std::vector<zecs::Entity> visited;
        for (zecs::Entity e : world.view_comp_group<Packed, test::Position>())
        {
            visited.push_back(e);
            if (visited.size() % 2 == 0)
            {
                CHECK(world.comp_remove<Packed>(e));
                CHECK(world.comp_remove<test::Position>(e));
            }
        }

        CHECK(all_different(visited));
        CHECK(test::sorted(visited) == test::sorted(entities));
        CHECK(test::collect_entities(world.view_comp_group<Packed, test::Position>()).size() == 15);
    }

    SUBCASE("for_each can remove the current component")
    {
        const auto entities = fill<Packed>(world, 30);

        size_t calls = 0;
        const auto scope = world.query_scope<Packed>();
        world.query_for_each(scope, [&](zecs::Entity e, Packed& comp)
        {
            ++calls;
            if (comp.value % 3 == 0)
                world.comp_remove<Packed>(e);
        });

        CHECK(calls == entities.size());
        CHECK(world.comp_pool_get<Packed>()->size() == 20);
        for (size_t i = 0; i < entities.size(); ++i)
        {
            const Packed* comp = world.comp_get<Packed>(entities[i]);
            CHECK((comp == nullptr) == (i % 3 == 0));
            if (comp)
                CHECK(comp->value == static_cast<int>(i));
        }
    }

    SUBCASE("the patch list keeps its order when components move")
    {
        const auto entities = fill<Packed>(world, 6);
        const zecs::TickContext ctx(world.tick_get());

        // Patched from old to new: 4, 0, 5, 2.
        world.comp_patch<Packed>(entities[4]);
        world.comp_patch<Packed>(entities[0]);
        world.comp_patch<Packed>(entities[5]);
        world.comp_patch<Packed>(entities[2]);

        // 5 moves into the slot of 1, then 4 moves into the slot of 0.
        CHECK(world.comp_remove<Packed>(entities[1]));
        CHECK(world.comp_remove<Packed>(entities[0]));

        // The view goes from new to old.
        CHECK(test::collect_entities(world.view_patched_comps<Packed>(ctx))
            == std::vector<zecs::Entity>{ entities[2], entities[5], entities[4] });
    }

    SUBCASE("a patched pass can remove components")
    {
        const auto entities = fill<Packed>(world, 20);
        const zecs::TickContext ctx(world.tick_get());
        for (zecs::Entity e : entities)
            world.comp_patch<Packed>(e);

        std::vector<zecs::Entity> visited;
        for (zecs::Entity e : world.view_patched_comps<Packed>(ctx))
        {
            visited.push_back(e);
            CHECK(world.comp_remove<Packed>(e));
        }

        CHECK(test::sorted(visited) == test::sorted(entities));
        CHECK(world.view_comps<Packed>().empty());
    }

    SUBCASE("a patched pass can remove the components of other entities")
    {
        // The patched view goes by the patch list, not by the slots. So it
        // keeps the strict rule: no entity is skipped or visited two times,
        // also when the body removes a component that isn't visited yet.
        const auto entities = fill<Packed>(world, 40);
        const zecs::TickContext ctx(world.tick_get());
        for (zecs::Entity e : entities)
            world.comp_patch<Packed>(e);

        std::vector<zecs::Entity> visited;
        std::vector<zecs::Entity> removed;
        size_t next_target = 0;
        for (zecs::Entity e : world.view_patched_comps<Packed>(ctx))
        {
            visited.push_back(e);

            // The view goes from the last patched entity to the first one,
            // so the entities from the start of the list aren't visited yet.
            for (int k = 0; k < 2 && next_target < entities.size(); ++k, ++next_target)
            {
                const zecs::Entity target = entities[next_target];
                if (target != e && world.comp_remove<Packed>(target))
                    removed.push_back(target);
            }
        }

        CHECK(all_different(visited));
        for (zecs::Entity e : entities)
        {
            const bool was_visited = std::find(visited.begin(), visited.end(), e) != visited.end();
            const bool was_removed = std::find(removed.begin(), removed.end(), e) != removed.end();
            CHECK((was_visited || was_removed));
        }

        for (size_t i = 0; i < entities.size(); ++i)
        {
            if (const Packed* comp = world.comp_get<Packed>(entities[i]))
                CHECK(comp->value == static_cast<int>(i));
        }
    }

    SUBCASE("a handler of on_removed can remove the other components of the entity")
    {
        const auto entities = fill<Packed>(world, 5);
        for (zecs::Entity e : entities)
            world.comp_add<test::Position>(e);

        RemovePositionProbe probe;
        world.comp_on_removed<Packed>().Connect(&probe, &RemovePositionProbe::on_signal);

        CHECK(world.comp_remove<Packed>(entities[1]));

        CHECK(probe.calls == 1);
        CHECK(world.comp_get<Packed>(entities[1]) == nullptr);
        CHECK(world.comp_get<test::Position>(entities[1]) == nullptr);
        CHECK(world.comp_pool_get<test::Position>()->size() == 4);
        for (int i : { 0, 2, 3, 4 })
            CHECK(world.comp_get<Packed>(entities[i])->value == i);
    }

    SUBCASE("compact frees the memory of a packed pool")
    {
        const auto entities = fill<Packed>(world, 10000);
        for (size_t i = 100; i < entities.size(); ++i)
            world.entity_remove(entities[i]);

        world.compact();

        for (int i = 0; i < 100; ++i)
            CHECK(world.comp_get<Packed>(entities[i])->value == i);

        world.comp_add<Packed>(world.entity_add())->value = 5;
        CHECK(world.comp_pool_get<Packed>()->size() == 101);
    }
}

// A pool with stable pointers keeps its components in place.
TEST_CASE("stable pool")
{
    zecs::World world;
    const auto entities = fill<Stable>(world, 5);

    std::vector<const Stable*> pointers;
    for (zecs::Entity e : entities)
        pointers.push_back(world.comp_get<Stable>(e));

    CHECK(world.comp_remove<Stable>(entities[1]));
    CHECK(world.comp_remove<Stable>(entities[3]));

    SUBCASE("pointers stay valid and the slots stay empty")
    {
        CHECK(has_empty_slots<Stable>(world));
        for (int i : { 0, 2, 4 })
        {
            CHECK(world.comp_get<Stable>(entities[i]) == pointers[i]);
            CHECK(pointers[i]->value == i);
        }
    }

    SUBCASE("a pass does not fill the empty slots")
    {
        for (zecs::Entity e : world.view_comps<Stable>())
            CHECK(world.comp_get<Stable>(e) != nullptr);

        CHECK(has_empty_slots<Stable>(world));
        CHECK(world.comp_get<Stable>(entities[4]) == pointers[4]);
    }

    SUBCASE("a pass can remove the component of an entity that is not visited yet")
    {
        // Nothing moves in this pool, so the body can remove any component.
        std::vector<zecs::Entity> visited;
        zecs::Entity future = zecs::Entity_INVALID;
        for (zecs::Entity e : world.view_comps<Stable>())
        {
            visited.push_back(e);
            if (future == zecs::Entity_INVALID)
            {
                // The pass goes from the last slot, so the first entity is the last one it visits.
                future = entities[0];
                REQUIRE(e != future);
                CHECK(world.comp_remove<Stable>(future));
            }
        }

        CHECK(test::sorted(visited) == test::sorted({ entities[2], entities[4] }));
    }

    SUBCASE("a new component takes an empty slot")
    {
        const zecs::Entity added = world.entity_add();
        const Stable* comp = world.comp_add<Stable>(added);

        CHECK((comp == pointers[1] || comp == pointers[3]));
        CHECK(world.comp_get<Stable>(entities[4]) == pointers[4]);
    }
}

// The body removes the node that the patched pass visits next. The pass
// must find the node after it without a search through the whole list.
TEST_CASE("patched pass stays fast when the body removes the next node")
{
    for (bool stable : { false, true })
    {
        zecs::World world;
        const int count = 40000;

        std::vector<zecs::Entity> entities;
        for (int i = 0; i < count; ++i)
        {
            entities.push_back(world.entity_add());
            if (stable)
                world.comp_add<Stable>(entities.back());
            else
                world.comp_add<Packed>(entities.back());
        }

        const zecs::GUid guid = stable ? zecs::globalUid<Stable> : zecs::globalUid<Packed>;
        const zecs::TickContext ctx(world.tick_get());
        for (zecs::Entity e : entities)
            world.comp_patch(e, guid);

        // The pass goes from the last patched entity to the first one.
        size_t visited = 0;
        size_t next = entities.size();
        for (zecs::Entity e : world.view_patched_comps(guid, ctx))
        {
            ++visited;
            next -= 2;
            CHECK(e == entities[next + 1]);
            CHECK(world.comp_remove(entities[next], guid));
        }

        CHECK(visited == entities.size() / 2);
    }
}
