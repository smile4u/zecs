#include "doctest/doctest.h"

#include "test_common_api.h"

#include <string>
#include <vector>

namespace
{

struct SignalLog
{
    std::vector<std::string> order;
    int added_calls = 0;
    int removed_calls = 0;
    int patched_calls = 0;
};

struct SignalProbe
{
    void on_position_added_add_velocity(zecs::World& world, zecs::Entity entity)
    {
        // Reentrant add: the callback mutates the same entity while the outer add
        // is still unwinding. This is the most important signal-race analogue from
        // entt for the current zecs API.
        log->order.emplace_back("position/added");
        ++log->added_calls;

        if (world.comp_get<test::Velocity>(entity) == nullptr)
            world.comp_add<test::Velocity>(entity, 7, 9);
    }

    void on_velocity_added_record(zecs::World&, zecs::Entity)
    {
        log->order.emplace_back("velocity/added");
        ++log->added_calls;
    }

    void on_position_removed_remove_velocity(zecs::World& world, zecs::Entity entity)
    {
        // Reentrant remove: destroying one component must be allowed to trigger
        // cleanup of related components on the same entity.
        log->order.emplace_back("position/removed");
        ++log->removed_calls;
        world.comp_remove<test::Velocity>(entity);
    }

    void on_velocity_removed_record(zecs::World&, zecs::Entity)
    {
        log->order.emplace_back("velocity/removed");
        ++log->removed_calls;
    }

    void on_position_patched_add_tag(zecs::World& world, zecs::Entity entity)
    {
        // Reentrant patch: patch callbacks often drive reactive systems.
        // Here we assert that patching a component can safely create another
        // component without corrupting the current signal delivery.
        log->order.emplace_back("position/patched");
        ++log->patched_calls;

        if (world.comp_get<test::Tag>(entity) == nullptr)
            world.comp_add<test::Tag>(entity);
    }

    SignalLog* log = nullptr;
};

} // namespace

// Covers the most important signal reentrancy scenarios:
// add callbacks that add more components, remove cascades and patch callbacks with side effects.
TEST_CASE("signals_reentrant")
{
    SUBCASE("added signal can create a dependent component on the same entity")
    {
        // The callback chain should run in a stable order and leave the entity in
        // the fully populated state expected by reactive setup code.
        zecs::World world("signals-add");
        const zecs::Entity entity = world.entity_add();

        SignalLog log;
        SignalProbe probe{ &log };

        world.comp_on_added<test::Position>().Connect(&probe, &SignalProbe::on_position_added_add_velocity);
        world.comp_on_added<test::Velocity>().Connect(&probe, &SignalProbe::on_velocity_added_record);

        REQUIRE(world.comp_add<test::Position>(entity, 1, 2) != nullptr);

        CHECK(world.comp_get<test::Position>(entity) != nullptr);
        CHECK(world.comp_get<test::Velocity>(entity) != nullptr);
        CHECK(log.added_calls == 2);

        const std::vector<std::string> expected_order = {
            "position/added",
            "velocity/added"
        };
        CHECK(log.order == expected_order);
    }

    SUBCASE("removed signal can cascade removal of another component")
    {
        // This covers the most practical cleanup pattern: once the primary
        // component goes away, a dependent component is removed from the same
        // entity inside the callback.
        zecs::World world("signals-remove");
        const zecs::Entity entity = world.entity_add();

        REQUIRE(world.comp_add<test::Position>(entity, 3, 4) != nullptr);
        REQUIRE(world.comp_add<test::Velocity>(entity, 5, 6) != nullptr);

        SignalLog log;
        SignalProbe probe{ &log };

        world.comp_on_removed<test::Position>().Connect(&probe, &SignalProbe::on_position_removed_remove_velocity);
        world.comp_on_removed<test::Velocity>().Connect(&probe, &SignalProbe::on_velocity_removed_record);

        CHECK(world.comp_remove<test::Position>(entity));

        CHECK(world.comp_get<test::Position>(entity) == nullptr);
        CHECK(world.comp_get<test::Velocity>(entity) == nullptr);
        CHECK(log.removed_calls == 2);

        const std::vector<std::string> expected_order = {
            "position/removed",
            "velocity/removed"
        };
        CHECK(log.order == expected_order);
    }

    SUBCASE("patched signal can attach a tag and missing patch does not emit")
    {
        // The first patch is a no-op because the entity has no Position yet.
        // After the component exists, patch should emit exactly once and the
        // callback should be able to add a tag component safely.
        zecs::World world("signals-patch");
        const zecs::Entity entity = world.entity_add();

        SignalLog log;
        SignalProbe probe{ &log };

        world.comp_on_patched<test::Position>().Connect(&probe, &SignalProbe::on_position_patched_add_tag);

        world.comp_patch<test::Position>(entity);
        CHECK(log.patched_calls == 0);
        CHECK(world.comp_get<test::Tag>(entity) == nullptr);

        REQUIRE(world.comp_add<test::Position>(entity, 8, 13) != nullptr);
        world.comp_patch<test::Position>(entity);

        CHECK(log.patched_calls == 1);
        CHECK(world.comp_get<test::Tag>(entity) != nullptr);

        const std::vector<std::string> expected_order = {
            "position/patched"
        };
        CHECK(log.order == expected_order);
    }
}
