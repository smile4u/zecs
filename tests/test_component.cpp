#include "doctest/doctest.h"

#include "test_common_api.h"

// Covers typed and runtime component operations on a populated world:
// add/get/get_or_add/remove, singleton helpers, pool lifecycle and destruction behavior.
TEST_CASE("component_api")
{
    test::TrackedLifetime::reset();

    {
        test::ComponentWorldFixture fx;

        SUBCASE("components can be fetched and removed")
        {
            auto* position = fx.world.comp_get<test::Position>(fx.position_entity);
            REQUIRE(position != nullptr);
            CHECK(position->x == 10);
            CHECK(position->y == 20);

            CHECK(fx.world.comp_remove<test::Position>(fx.position_entity));
            CHECK(fx.world.comp_get<test::Position>(fx.position_entity) == nullptr);
            CHECK_FALSE(fx.world.comp_remove<test::Position>(fx.position_entity));
        }

        SUBCASE("get_or_add creates missing component and returns existing instance")
        {
            auto* first = fx.world.comp_get_or_add<test::Position>(fx.get_or_add_entity);
            REQUIRE(first != nullptr);
            first->x = 7;
            first->y = 11;

            auto* second = fx.world.comp_get_or_add<test::Position>(fx.get_or_add_entity);
            REQUIRE(second != nullptr);

            CHECK(second == first);
            CHECK(second->x == 7);
            CHECK(second->y == 11);
        }

        SUBCASE("singleton component helpers operate on root entity")
        {
            auto* fetched = fx.world.comp_get_sc<test::Velocity>();
            REQUIRE(fetched != nullptr);

            CHECK(fetched->dx == 3);
            CHECK(fetched->dy == 5);
            CHECK(fx.world.comp_remove_sc<test::Velocity>());
            CHECK(fx.world.comp_get_sc<test::Velocity>() == nullptr);
        }

        SUBCASE("empty and non default constructible components are available through typed API")
        {
            auto* tag = fx.world.comp_get<test::Tag>(fx.tag_entity);
            REQUIRE(tag != nullptr);
            CHECK(fx.world.comp_get<test::Tag>(fx.tag_entity) == tag);

            auto* non_default = fx.world.comp_get<test::NonDefault>(fx.tag_entity);
            REQUIRE(non_default != nullptr);
            CHECK(non_default->value == 42);
        }

        SUBCASE("removing entity removes all its components")
        {
            fx.world.entity_remove(fx.removable_entity);

            CHECK(fx.world.comp_get<test::Position>(fx.removable_entity) == nullptr);
            CHECK(fx.world.comp_get<test::Velocity>(fx.removable_entity) == nullptr);
        }

        SUBCASE("runtime guid API supports add get remove and views for empty components")
        {
            const zecs::GUid tag_guid = zecs::globalUid<test::Tag>;

            auto* added = fx.world.comp_add(fx.get_or_add_entity, tag_guid);
            REQUIRE(added != nullptr);
            CHECK(fx.world.comp_get(fx.get_or_add_entity, tag_guid) == added);

            const auto entities = test::collect_entities(fx.world.view_comps(tag_guid));
            REQUIRE(entities.size() == 2);
            CHECK(entities[0] == fx.tag_entity);
            CHECK(entities[1] == fx.get_or_add_entity);

            CHECK(fx.world.comp_remove(fx.get_or_add_entity, tag_guid));
            CHECK(fx.world.comp_get(fx.get_or_add_entity, tag_guid) == nullptr);
        }

        SUBCASE("runtime guid add returns null for non default constructible component")
        {
            const zecs::GUid non_default_guid = zecs::globalUid<test::NonDefault>;

            CHECK(fx.world.comp_add(fx.get_or_add_entity, non_default_guid) == nullptr);
            CHECK(fx.world.comp_get(fx.get_or_add_entity, non_default_guid) == nullptr);
        }

        SUBCASE("tag component can be removed and added again")
        {
            CHECK(fx.world.comp_remove<test::Tag>(fx.tag_entity));
            CHECK(fx.world.comp_get<test::Tag>(fx.tag_entity) == nullptr);

            auto* tag = fx.world.comp_add<test::Tag>(fx.tag_entity);
            REQUIRE(tag != nullptr);
            CHECK(fx.world.comp_get<test::Tag>(fx.tag_entity) == tag);
        }

        SUBCASE("singleton and regular components of same type coexist independently")
        {
            auto* singleton_velocity = fx.world.comp_get_sc<test::Velocity>();
            auto* regular_velocity = fx.world.comp_get<test::Velocity>(fx.removable_entity);

            REQUIRE(singleton_velocity != nullptr);
            REQUIRE(regular_velocity != nullptr);
            CHECK(singleton_velocity != regular_velocity);

            CHECK(fx.world.comp_remove_sc<test::Velocity>());
            CHECK(fx.world.comp_get_sc<test::Velocity>() == nullptr);
            CHECK(fx.world.comp_get<test::Velocity>(fx.removable_entity) == regular_velocity);
        }

        SUBCASE("component lifetime updates immediately on remove")
        {
            CHECK(test::TrackedLifetime::alive == 2);
            CHECK(test::TrackedLifetime::constructed == 2);
            CHECK(test::TrackedLifetime::destroyed == 0);

            CHECK(fx.world.comp_remove<test::TrackedLifetime>(fx.tracked_first));
            CHECK(test::TrackedLifetime::alive == 1);
            CHECK(test::TrackedLifetime::destroyed == 1);
        }

        SUBCASE("component pools are created lazily and persist after last component removal")
        {
            // This block documents the pool lifecycle that zecs exposes through
            // `comp_pool_get()` and `view_pools()`: pools do not exist before the
            // first add, but once created they remain registered even if all
            // components of that type are removed later.
            zecs::World world("pool-lifecycle");
            const zecs::Entity entity = world.entity_add();
            const zecs::GUid position_guid = zecs::globalUid<test::Position>;

            CHECK(world.comp_pool_get<test::Position>() == nullptr);
            CHECK(world.comp_pool_get(position_guid) == nullptr);
            CHECK(test::collect_pools(world.view_pools()).empty());

            auto* added = world.comp_add<test::Position>(entity, 1, 2);
            REQUIRE(added != nullptr);

            auto* typed_pool = world.comp_pool_get<test::Position>();
            auto* runtime_pool = world.comp_pool_get(position_guid);
            REQUIRE(typed_pool != nullptr);
            CHECK(runtime_pool == typed_pool);

            const auto pools_after_add = test::collect_pools(world.view_pools());
            REQUIRE(pools_after_add.size() == 1);
            CHECK(pools_after_add[0] == typed_pool);

            CHECK(world.comp_remove<test::Position>(entity));
            CHECK(world.comp_get<test::Position>(entity) == nullptr);

            const auto pools_after_remove = test::collect_pools(world.view_pools());
            REQUIRE(pools_after_remove.size() == 1);
            CHECK(pools_after_remove[0] == typed_pool);

            auto* tag = world.comp_add<test::Tag>(entity);
            REQUIRE(tag != nullptr);

            const auto pools_after_second_type = test::collect_pools(world.view_pools());
            REQUIRE(pools_after_second_type.size() == 2);
            CHECK(world.comp_pool_get<test::Tag>() != nullptr);
        }
    }

    CHECK(test::TrackedLifetime::alive == 0);
    CHECK(test::TrackedLifetime::constructed == 2);
    CHECK(test::TrackedLifetime::destroyed == 2);
}
