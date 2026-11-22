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
        // An index has this number of generations. After the last one
        // a new entity takes another index: there is a test for it below.
        const int generations = 1 << (32 - zecs::Entity_INDEX_BITS);
        for (int i = 0; i < generations; ++i)
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

        const auto scope = world.query_scope<test::Position>();
        CHECK_FALSE(world.query_has_all(old_entity, scope));
        CHECK_FALSE(world.query_has_any(old_entity, scope));

        // A remove with the old ID doesn't remove the new entity.
        CHECK_FALSE(world.entity_remove(old_entity));
        CHECK(world.entity_is_valid(new_entity));
        CHECK(world.query_has_all(new_entity, scope));

        // A patch with the old ID does nothing.
        const zecs::TickContext ctx(world.tick_get());
        world.comp_patch<test::Position>(old_entity);
        CHECK(test::collect_entities(world.view_patched_comps<test::Position>(ctx)).empty());

        REQUIRE(world.comp_get<test::Position>(new_entity) != nullptr);
        CHECK(world.comp_get<test::Position>(new_entity)->x == 9);
    }

    SUBCASE("an index that used its last generation is never used again")
    {
        const zecs::Entity first = world.entity_add();
        const zecs::EntityBase index = zecs::entity_to_index(first);
        world.comp_add<test::Position>(first);

        // Every generation of the index is used one time.
        const int generations = 1 << (32 - zecs::Entity_INDEX_BITS);
        zecs::Entity e = first;
        for (int i = 1; i < generations; ++i)
        {
            REQUIRE(world.entity_remove(e));
            e = world.entity_add();
            REQUIRE(zecs::entity_to_index(e) == index);
            REQUIRE(e != first);
        }

        // `e` has the last generation. A new entity takes another index.
        world.comp_add<test::Position>(e);
        CHECK(world.entity_remove(e));

        const zecs::Entity next = world.entity_add();
        CHECK(world.entity_is_valid(next));
        CHECK(zecs::entity_to_index(next) != index);

        // The old IDs of the index stay not valid.
        CHECK_FALSE(world.entity_is_valid(first));
        CHECK_FALSE(world.entity_is_valid(e));
        CHECK_FALSE(world.entity_remove(e));
        CHECK(world.comp_get<test::Position>(first) == nullptr);
        CHECK(world.comp_get<test::Position>(e) == nullptr);

        // A pass over the entities skips the slot of this index.
        const auto entities = test::collect_entities(world.view_entities());
        REQUIRE(entities.size() == 2);
        CHECK(entities[0] == world.root());
        CHECK(entities[1] == next);
        CHECK(world.counters_get().entities == 2);
    }

    SUBCASE("entity_add returns Entity_INVALID when no index is left")
    {
        // The root has one index already.
        const size_t limit = zecs::Entity_INDEX_MASK;
        zecs::Entity last = zecs::Entity_INVALID;
        for (size_t i = 1; i < limit; ++i)
        {
            last = world.entity_add();
            REQUIRE(last != zecs::Entity_INVALID);
        }

        CHECK(world.counters_get().entities == limit);
        CHECK(world.entity_add() == zecs::Entity_INVALID);
        CHECK(world.entity_add() == zecs::Entity_INVALID);
        CHECK(world.counters_get().entities == limit);

        // A removed entity gives its index to a new one.
        CHECK(world.entity_remove(last));
        const zecs::Entity again = world.entity_add();
        CHECK(world.entity_is_valid(again));
        CHECK(zecs::entity_to_index(again) == zecs::entity_to_index(last));
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

        // Entities added during the loop aren't visited.
        CHECK(visited == 4);
    }
}

TEST_CASE("world_counters")
{
    zecs::World world;

    CHECK(world.counters_get().entities == 1); // the root
    CHECK(world.counters_get().components == 0);
    CHECK(world.counters_get().pools == 0);

    const zecs::Entity a = world.entity_add();
    const zecs::Entity b = world.entity_add();
    world.comp_add<test::Position>(a);
    world.comp_add<test::Position>(b);
    world.comp_add<test::Velocity>(a);

    CHECK(world.counters_get().entities == 3);
    CHECK(world.counters_get().components == 3);
    CHECK(world.counters_get().pools == 2);

    // A pool stays when its last component is removed.
    world.comp_remove<test::Velocity>(a);
    CHECK(world.counters_get().components == 2);
    CHECK(world.counters_get().pools == 2);

    world.entity_remove(b);
    CHECK(world.counters_get().entities == 2);
    CHECK(world.counters_get().components == 1);
}
