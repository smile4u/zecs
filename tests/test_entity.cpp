#include "doctest/doctest.h"

#include "test_common_api.h"

// Covers the core World entity lifecycle on a small pre-populated fixture:
// root handling, names, iteration order, removal visibility and validity checks.
TEST_CASE("entity_api")
{
    test::EntityWorldFixture fx;

    SUBCASE("world exposes root and name management")
    {
        CHECK(fx.world.root() != zecs::Entity_INVALID);
        CHECK(fx.world.entity_is_valid(fx.world.root()));
        CHECK(doctest::String(fx.world.name_get()) == "main");

        fx.world.name_set("renamed");
        CHECK(doctest::String(fx.world.name_get()) == "renamed");

        fx.world.name_set(nullptr);
        CHECK(doctest::String(fx.world.name_get()) == "");
    }

    SUBCASE("view_entities returns all live entities in creation order")
    {
        const auto entities = test::collect_entities(fx.world.view_entities());

        REQUIRE(entities.size() == 4);
        CHECK(entities[0] == fx.world.root());
        CHECK(entities[1] == fx.first);
        CHECK(entities[2] == fx.second);
        CHECK(entities[3] == fx.third);
    }

    SUBCASE("entity_remove hides removed entities from iteration")
    {
        fx.world.entity_remove(fx.first);

        const auto entities = test::collect_entities(fx.world.view_entities());

        REQUIRE(entities.size() == 3);
        CHECK(entities[0] == fx.world.root());
        CHECK(entities[1] == fx.second);
        CHECK(entities[2] == fx.third);
    }

    SUBCASE("entity validity rejects removed invalid and out of range entities")
    {
        fx.world.entity_remove(fx.first);

        CHECK_FALSE(fx.world.entity_is_valid(fx.first));
        CHECK_FALSE(fx.world.entity_is_valid(zecs::Entity_INVALID));
        CHECK_FALSE(fx.world.entity_is_valid(static_cast<zecs::Entity>(999999)));
        CHECK(fx.world.entity_is_valid(fx.second));
    }

    SUBCASE("entity_remove is idempotent for already removed entities")
    {
        CHECK(fx.world.entity_remove(fx.first));
        CHECK_FALSE(fx.world.entity_remove(fx.first));
        CHECK_FALSE(fx.world.entity_remove(zecs::Entity_INVALID));

        const auto entities = test::collect_entities(fx.world.view_entities());

        REQUIRE(entities.size() == 3);
        CHECK(entities[0] == fx.world.root());
        CHECK(entities[1] == fx.second);
        CHECK(entities[2] == fx.third);
    }

    SUBCASE("entity_remove does not remove root")
    {
        CHECK_FALSE(fx.world.entity_remove(fx.world.root()));

        const auto entities = test::collect_entities(fx.world.view_entities());

        REQUIRE(entities.size() == 4);
        CHECK(entities[0] == fx.world.root());
        CHECK(entities[1] == fx.first);
        CHECK(entities[2] == fx.second);
        CHECK(entities[3] == fx.third);
    }
}

// A removed entity gives its index to the next new entity.
// The generation part of the ID tells the old and the new entity apart.
TEST_CASE("entity_reuse")
{
    zecs::World world;

    SUBCASE("new entity takes the index of a removed one, with a new ID")
    {
        const zecs::Entity old_entity = world.entity_add();
        world.entity_remove(old_entity);
        const zecs::Entity new_entity = world.entity_add();

        CHECK(zecs::entity_to_index(new_entity) == zecs::entity_to_index(old_entity));
        CHECK(new_entity != old_entity);
        CHECK(new_entity != zecs::Entity_INVALID);
        CHECK_FALSE(world.entity_is_valid(old_entity));
        CHECK(world.entity_is_valid(new_entity));
    }

    SUBCASE("the last removed index is taken first")
    {
        const zecs::Entity a = world.entity_add();
        const zecs::Entity b = world.entity_add();
        const zecs::Entity c = world.entity_add();
        world.entity_remove(a);
        world.entity_remove(b);
        world.entity_remove(c);

        CHECK(zecs::entity_to_index(world.entity_add()) == zecs::entity_to_index(c));
        CHECK(zecs::entity_to_index(world.entity_add()) == zecs::entity_to_index(b));
        CHECK(zecs::entity_to_index(world.entity_add()) == zecs::entity_to_index(a));

        // No free index is left, so the next one is new.
        CHECK(zecs::entity_to_index(world.entity_add()) == zecs::entity_to_index(c) + 1);
    }

    SUBCASE("indices do not grow when entities are added and removed in turn")
    {
        for (int i = 0; i < 10000; ++i)
        {
            const zecs::Entity e = world.entity_add();
            world.comp_add<test::Position>(e);
            CHECK(zecs::entity_to_index(e) == 1); // 0 is the root
            world.entity_remove(e);
        }
    }

    SUBCASE("new entity does not get the components of the removed one")
    {
        const zecs::Entity old_entity = world.entity_add();
        world.comp_add<test::Position>(old_entity)->x = 7;
        world.entity_remove(old_entity);

        const zecs::Entity new_entity = world.entity_add();
        CHECK(world.comp_get<test::Position>(new_entity) == nullptr);

        world.comp_add<test::Position>(new_entity)->x = 9;
        REQUIRE(world.comp_get<test::Position>(new_entity) != nullptr);
        CHECK(world.comp_get<test::Position>(new_entity)->x == 9);

        // Views give the full ID, not only the index.
        const auto entities = test::collect_entities(world.view_comps<test::Position>());
        REQUIRE(entities.size() == 1);
        CHECK(entities[0] == new_entity);
    }

    SUBCASE("old ID does not reach the components of the new entity")
    {
        const zecs::Entity old_entity = world.entity_add();
        world.comp_add<test::Position>(old_entity);
        world.entity_remove(old_entity);

        const zecs::Entity new_entity = world.entity_add();
        world.comp_add<test::Position>(new_entity)->x = 9;

        CHECK(world.comp_get<test::Position>(old_entity) == nullptr);
        CHECK(world.comp_get(old_entity, zecs::globalUid<test::Position>) == nullptr);
        CHECK_FALSE(world.comp_remove<test::Position>(old_entity));
        CHECK_FALSE(world.comp_remove(old_entity, zecs::globalUid<test::Position>));

        const auto tuple = world.comps_tuple_get<test::Position>();
        CHECK_FALSE(world.has_all(old_entity, tuple));
        CHECK_FALSE(world.has_any(old_entity, tuple));

        // A remove with the old ID does not remove the new entity.
        CHECK_FALSE(world.entity_remove(old_entity));
        CHECK(world.entity_is_valid(new_entity));
        CHECK(world.has_all(new_entity, tuple));

        // A patch with the old ID does nothing.
        const zecs::TickContext ctx(world.tick_get());
        world.comp_patch<test::Position>(old_entity);
        CHECK(test::collect_entities(world.view_patched_comps<test::Position>(ctx)).empty());

        REQUIRE(world.comp_get<test::Position>(new_entity) != nullptr);
        CHECK(world.comp_get<test::Position>(new_entity)->x == 9);
    }

    SUBCASE("generation starts again from the first one when it runs out")
    {
        const zecs::Entity first = world.entity_add();
        zecs::Entity e = first;

        const int generations = 1 << (32 - zecs::Entity_INDEX_BITS);
        for (int i = 0; i < generations; ++i)
        {
            world.entity_remove(e);
            e = world.entity_add();
            CHECK(e != zecs::Entity_INVALID);
            CHECK(world.entity_is_valid(e));
        }

        CHECK(e == first);
    }

    SUBCASE("view_entities skips removed entities and gives reused ones")
    {
        const zecs::Entity a = world.entity_add();
        const zecs::Entity b = world.entity_add();
        const zecs::Entity c = world.entity_add();
        world.entity_remove(a);
        world.entity_remove(b);

        auto entities = test::collect_entities(world.view_entities());
        REQUIRE(entities.size() == 2);
        CHECK(entities[0] == world.root());
        CHECK(entities[1] == c);

        const zecs::Entity d = world.entity_add(); // takes the index of b
        entities = test::collect_entities(world.view_entities());
        REQUIRE(entities.size() == 3);
        CHECK(entities[0] == world.root());
        CHECK(entities[1] == d);
        CHECK(entities[2] == c);
    }

    SUBCASE("view_entities allows adding entities during traversal")
    {
        for (int i = 0; i < 3; ++i)
            world.entity_add();

        // Many adds, so the entity list moves in memory during the loop.
        size_t visited = 0;
        for (zecs::Entity entity : world.view_entities())
        {
            CHECK(world.entity_is_valid(entity));
            ++visited;

            for (int i = 0; i < 100; ++i)
                world.entity_add();
        }

        // Entities added during the loop are not visited.
        CHECK(visited == 4);
    }
}
