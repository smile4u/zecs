#include "doctest/doctest.h"

#include "test_common_api.h"

#include <type_traits>
#include <utility>

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
