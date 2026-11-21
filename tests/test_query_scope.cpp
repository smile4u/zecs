#include "doctest/doctest.h"

#include "test_common_api.h"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>

// QueryScope: checks for several component types with the pools found one time.
// The checks are in the base, which knows only the number of the types.
static_assert(std::is_same_v<decltype(std::declval<zecs::World&>().query_scope<test::Position, test::Velocity>()),
    zecs::QueryScope<test::Position, test::Velocity>>);
static_assert(std::is_base_of_v<zecs::QueryScopeBase<2>, zecs::QueryScope<test::Position, test::Velocity>>);

TEST_CASE("query scope")
{
    zecs::World world;
    const zecs::Entity none = world.entity_add();
    const zecs::Entity only_position = world.entity_add();
    const zecs::Entity both = world.entity_add();
    const zecs::Entity all = world.entity_add();

    SUBCASE("scope made before the first component works after it is added")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity>();
        CHECK_FALSE(world.query_has_any(none, scope));

        world.comp_add<test::Position>(none);
        world.comp_add<test::Velocity>(none);

        CHECK(world.query_has_all(none, scope));
        world.comp_remove<test::Position>(none);
        world.comp_remove<test::Velocity>(none);
    }

    world.comp_add<test::Position>(only_position, 1, 2);
    world.comp_add<test::Position>(both, 3, 4);
    world.comp_add<test::Velocity>(both, 5, 6);
    world.comp_add<test::Position>(all, 7, 8);
    world.comp_add<test::Velocity>(all, 9, 10);
    world.comp_add<test::Tag>(all);

    SUBCASE("has_all and has_any")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity, test::Tag>();

        CHECK_FALSE(world.query_has_all(none, scope));
        CHECK_FALSE(world.query_has_any(none, scope));

        CHECK_FALSE(world.query_has_all(only_position, scope));
        CHECK(world.query_has_any(only_position, scope));

        CHECK_FALSE(world.query_has_all(both, scope));
        CHECK(world.query_has_any(both, scope));

        CHECK(world.query_has_all(all, scope));
        CHECK(world.query_has_any(all, scope));

        CHECK_FALSE(world.query_has_all(zecs::Entity_INVALID, scope));
        CHECK_FALSE(world.query_has_any(zecs::Entity_INVALID, scope));

        // An entity with an index above the end of sparse[] of two pools.
        const zecs::Entity late = world.entity_add();
        world.comp_add<test::Position>(late);
        CHECK_FALSE(world.query_has_all(late, scope));
        CHECK(world.query_has_any(late, scope));
    }

    SUBCASE("scope follows removed components and removed entities")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity>();
        const auto copy = scope;

        world.comp_remove<test::Velocity>(both);
        CHECK_FALSE(world.query_has_all(both, scope));
        CHECK(world.query_has_any(both, scope));

        world.entity_remove(all);
        CHECK_FALSE(world.query_has_any(all, scope));

        // The slot of the removed entity is used again with a new generation.
        const zecs::Entity next = world.entity_add();
        world.comp_add<test::Position>(next);
        CHECK(world.query_has_any(next, scope));
        CHECK_FALSE(world.query_has_any(all, scope));
        CHECK(world.query_has_any(next, copy));
    }
}

// query_for_each: the body gets every entity that has all components
// of the scope, together with the components.
TEST_CASE("query for_each")
{
    zecs::World world;

    // Every third entity has Position, every second has Velocity, every fifth has Tag.
    std::vector<zecs::Entity> entities;
    for (int i = 0; i < 60; ++i)
    {
        const zecs::Entity e = world.entity_add();
        entities.push_back(e);
        if (i % 3 == 0) world.comp_add<test::Position>(e, i, -i);
        if (i % 2 == 0) world.comp_add<test::Velocity>(e, i * 10, 0);
        if (i % 5 == 0) world.comp_add<test::Tag>(e);
    }

    // The entities that a group view gives: for_each must give the same.
    const auto expected = [&world](auto&& view)
    {
        auto list = test::collect_entities(view);
        std::sort(list.begin(), list.end());
        return list;
    };

    SUBCASE("gives the entities of the group with their components")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity>();

        std::vector<zecs::Entity> seen;
        world.query_for_each(scope, [&](zecs::Entity e, test::Position& p, test::Velocity& v)
        {
            seen.push_back(e);
            CHECK(&p == world.comp_get<test::Position>(e));
            CHECK(&v == world.comp_get<test::Velocity>(e));
            CHECK(v.dx == p.x * 10);
        });

        std::sort(seen.begin(), seen.end());
        CHECK(seen.size() == 10);
        CHECK(seen == expected(world.view_comp_group<test::Position, test::Velocity>()));
    }

    SUBCASE("the order of the types does not change the result")
    {
        const auto scope = world.query_scope<test::Velocity, test::Position>();

        std::vector<zecs::Entity> seen;
        world.query_for_each(scope, [&](zecs::Entity e, test::Velocity& v, test::Position& p)
        {
            seen.push_back(e);
            CHECK(v.dx == p.x * 10);
        });

        std::sort(seen.begin(), seen.end());
        CHECK(seen == expected(world.view_comp_group<test::Position, test::Velocity>()));
    }

    SUBCASE("a change made by the body stays in the component")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity>();
        world.query_for_each(scope, [](zecs::Entity, test::Position& p, const test::Velocity& v) { p.y = v.dx + 1; });

        for (zecs::Entity e : world.view_comp_group<test::Position, test::Velocity>())
            CHECK(world.comp_get<test::Position>(e)->y == world.comp_get<test::Velocity>(e)->dx + 1);
    }

    SUBCASE("a tag is given to the body like any other component")
    {
        const auto scope = world.query_scope<test::Position, test::Tag, test::Velocity>();

        std::vector<zecs::Entity> seen;
        world.query_for_each(scope, [&](zecs::Entity e, test::Position& p, test::Tag&, test::Velocity& v)
        {
            seen.push_back(e);
            CHECK(v.dx == p.x * 10);
        });

        std::sort(seen.begin(), seen.end());
        CHECK(seen.size() == 2); // entities 0 and 30
        CHECK(seen == expected(world.view_comp_group<test::Position, test::Velocity, test::Tag>()));
    }

    SUBCASE("one type: every component of the pool")
    {
        const auto scope = world.query_scope<test::Position>();

        int count = 0;
        world.query_for_each(scope, [&](zecs::Entity e, test::Position& p)
        {
            ++count;
            CHECK(&p == world.comp_get<test::Position>(e));
        });

        CHECK(count == 20);

        // An empty slot in the middle and at the end of the pool is skipped.
        world.comp_remove<test::Position>(entities[27]);
        world.comp_remove<test::Position>(entities[57]);
        count = 0;
        world.query_for_each(scope, [&](zecs::Entity e, test::Position&)
        {
            ++count;
            CHECK(e != entities[27]);
            CHECK(e != entities[57]);
        });

        CHECK(count == 18);
    }

    SUBCASE("a type that no entity has: the body is not called")
    {
        const auto scope = world.query_scope<test::Position, test::NonDefault>();

        int count = 0;
        world.query_for_each(scope, [&](zecs::Entity, test::Position&, test::NonDefault&) { ++count; });
        CHECK(count == 0);
    }

    SUBCASE("removed components and removed entities are skipped")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity>();

        world.comp_remove<test::Position>(entities[0]);
        world.comp_remove<test::Velocity>(entities[6]);
        world.entity_remove(entities[12]);

        std::vector<zecs::Entity> seen;
        world.query_for_each(scope, [&](zecs::Entity e, test::Position&, test::Velocity&) { seen.push_back(e); });

        std::sort(seen.begin(), seen.end());
        CHECK(seen.size() == 7);
        CHECK(seen == expected(world.view_comp_group<test::Position, test::Velocity>()));
    }

    SUBCASE("the result is the same with any pool as the smallest one")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity, test::Tag>();
        const auto count = [&]
        {
            int n = 0;
            world.query_for_each(scope, [&](zecs::Entity, test::Position&, test::Velocity&, test::Tag&) { ++n; });
            return n;
        };

        // Tag is the smallest pool now.
        CHECK(count() == 2);

        // Position becomes the smallest: only the entities 0 and 30 keep it.
        for (int i = 3; i < 60; i += 3)
            if (i != 30) world.comp_remove<test::Position>(entities[i]);
        CHECK(count() == 2);

        // Velocity becomes the smallest: only the entity 30 keeps it.
        for (int i = 0; i < 60; i += 2)
            if (i != 30) world.comp_remove<test::Velocity>(entities[i]);
        CHECK(count() == 1);
    }

    SUBCASE("the body can remove components and entities")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity>();

        // Every call removes the component of this entity, and one more
        // entity of the group. So the removed ones must not come later.
        std::vector<zecs::Entity> seen;
        world.query_for_each(scope, [&](zecs::Entity e, test::Position&, test::Velocity&)
        {
            CHECK(world.query_has_all(e, scope));
            seen.push_back(e);
            world.comp_remove<test::Position>(e);

            for (zecs::Entity other : entities)
            {
                if (world.query_has_all(other, scope))
                {
                    world.entity_remove(other);
                    break;
                }
            }
        });

        CHECK(seen.size() == 5);
        CHECK(world.view_comp_group<test::Position, test::Velocity>().empty());
    }

    SUBCASE("the body can add components")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity>();

        // Many new components: the arrays of both pools grow inside the loop.
        int count = 0;
        world.query_for_each(scope, [&](zecs::Entity e, test::Position& p, test::Velocity& v)
        {
            ++count;
            for (int i = 0; i < 500; ++i)
            {
                const zecs::Entity added = world.entity_add();
                world.comp_add<test::Position>(added, -1, -1);
                world.comp_add<test::Velocity>(added, -1, -1);
            }

            // The components of this call are still in place.
            CHECK(&p == world.comp_get<test::Position>(e));
            CHECK(&v == world.comp_get<test::Velocity>(e));
            CHECK(v.dx == p.x * 10);
        });

        // The components added by the body are not visited in the same pass.
        CHECK(count == 10);
    }

    SUBCASE("a copy of the scope and a body kept in a variable work")
    {
        const auto scope = world.query_scope<test::Position, test::Velocity>();
        const auto copy = scope;

        int count = 0;
        auto body = [&count](zecs::Entity, const test::Position&, test::Velocity) { ++count; };
        world.query_for_each(copy, body);
        world.query_for_each(copy, body);
        CHECK(count == 20);
    }
}

// The pass goes over the pool with the smallest number of dense slots.
// Empty slots are counted too: a pool with few components and many
// empty slots gives a long pass.
TEST_CASE("group pass goes over the pool with the smallest number of dense slots")
{
    zecs::World world;
    const zecs::Entity first = world.entity_add();
    const zecs::Entity second = world.entity_add();
    const zecs::Entity third = world.entity_add();
    const zecs::Entity extra = world.entity_add();

    // Position: 3 components and 8 empty slots after them.
    world.comp_add<test::Position>(first);
    world.comp_add<test::Position>(second);
    world.comp_add<test::Position>(third);
    // Add all of them first and remove them after that. A component
    // that is added after a removal takes the removed slot again.
    std::vector<zecs::Entity> temps(8);
    for (zecs::Entity& temp : temps)
    {
        temp = world.entity_add();
        world.comp_add<test::Position>(temp);
    }
    for (zecs::Entity temp : temps)
        world.entity_remove(temp);

    REQUIRE(world.comp_pool_get<test::Position>()->size() == 3);

    // Velocity: 4 components in the other order and no empty slots.
    world.comp_add<test::Velocity>(third);
    world.comp_add<test::Velocity>(second);
    world.comp_add<test::Velocity>(first);
    world.comp_add<test::Velocity>(extra);

    // The order of the entities shows which pool the pass goes over.
    const std::vector<zecs::Entity> velocity_order = { third, second, first };

    SUBCASE("typed view")
    {
        CHECK(test::collect_entities(world.view_comp_group<test::Position, test::Velocity>()) == velocity_order);
    }

    SUBCASE("runtime view")
    {
        const zecs::GUid guids[] = { zecs::globalUid<test::Position>, zecs::globalUid<test::Velocity> };
        CHECK(test::collect_entities(world.view_comp_group(guids, 2)) == velocity_order);
    }

    SUBCASE("for_each")
    {
        // query_for_each goes from the last slot to the first one.
        std::vector<zecs::Entity> visited;
        const auto scope = world.query_scope<test::Position, test::Velocity>();
        world.query_for_each(scope, [&](zecs::Entity e, test::Position&, test::Velocity&) { visited.push_back(e); });

        CHECK(visited == std::vector<zecs::Entity>{ first, second, third });
    }
}
