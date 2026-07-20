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
        fx.world.entity_remove(fx.first);
        fx.world.entity_remove(fx.first);

        const auto entities = test::collect_entities(fx.world.view_entities());

        REQUIRE(entities.size() == 3);
        CHECK(entities[0] == fx.world.root());
        CHECK(entities[1] == fx.second);
        CHECK(entities[2] == fx.third);
    }

    SUBCASE("entity_remove does not remove root")
    {
        fx.world.entity_remove(fx.world.root());

        const auto entities = test::collect_entities(fx.world.view_entities());

        REQUIRE(entities.size() == 4);
        CHECK(entities[0] == fx.world.root());
        CHECK(entities[1] == fx.first);
        CHECK(entities[2] == fx.second);
        CHECK(entities[3] == fx.third);
    }
}
