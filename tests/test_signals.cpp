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
        // entt for the current ZECS API.
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

template<int Index>
struct NewPool
{
    int value = 0;
};

// Handlers that change the world while entity_remove is running.
struct EntityRemoveProbe
{
    void on_removed_make_pools(zecs::World& world, zecs::Entity)
    {
        // Every type is new for the world, so the list of pools grows.
        world.comp_add<NewPool<0>>(world.root());
        world.comp_add<NewPool<1>>(world.root());
        world.comp_add<NewPool<2>>(world.root());
        world.comp_add<NewPool<3>>(world.root());
        world.comp_add<NewPool<4>>(world.root());
        world.comp_add<NewPool<5>>(world.root());
        world.comp_add<NewPool<6>>(world.root());
        world.comp_add<NewPool<7>>(world.root());
    }

    void on_removed_remove_other(zecs::World& world, zecs::Entity entity)
    {
        ++calls;
        valid_in_handler = world.entity_is_valid(entity);
        velocity_in_handler = world.comp_get<test::Velocity>(entity) != nullptr;
        other_removed = world.entity_remove(other);
    }

    void on_removed_add_entity(zecs::World& world, zecs::Entity entity)
    {
        valid_in_handler = world.entity_is_valid(entity);
        added = world.entity_add();
    }

    int calls = 0;
    bool valid_in_handler = false;
    bool velocity_in_handler = false;
    bool other_removed = false;
    zecs::Entity other = zecs::Entity_INVALID;
    zecs::Entity added = zecs::Entity_INVALID;
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

// A handler of on_removed can change the world while entity_remove is running.
TEST_CASE("entity_remove_reentrant")
{
    SUBCASE("a handler can make new pools")
    {
        zecs::World world("remove-new-pools");
        const zecs::Entity entity = world.entity_add();

        REQUIRE(world.comp_add<test::Position>(entity, 1, 2) != nullptr);
        REQUIRE(world.comp_add<test::Velocity>(entity, 3, 4) != nullptr);

        EntityRemoveProbe probe;
        world.comp_on_removed<test::Position>().Connect(&probe, &EntityRemoveProbe::on_removed_make_pools);

        CHECK(world.entity_remove(entity));

        CHECK(world.view_comps<test::Position>().empty());
        CHECK(world.view_comps<test::Velocity>().empty());
        CHECK(world.comp_get_sc<NewPool<7>>() != nullptr);
    }

    SUBCASE("a handler sees a valid entity and can remove another one")
    {
        zecs::World world("remove-other");
        const zecs::Entity entity = world.entity_add();
        const zecs::Entity other = world.entity_add();

        REQUIRE(world.comp_add<test::Position>(entity, 1, 2) != nullptr);
        REQUIRE(world.comp_add<test::Velocity>(entity, 3, 4) != nullptr);
        REQUIRE(world.comp_add<test::Velocity>(other, 5, 6) != nullptr);

        EntityRemoveProbe probe;
        probe.other = other;
        world.comp_on_removed<test::Position>().Connect(&probe, &EntityRemoveProbe::on_removed_remove_other);

        CHECK(world.entity_remove(entity));

        CHECK(probe.calls == 1);
        CHECK(probe.valid_in_handler);
        CHECK(probe.velocity_in_handler);
        CHECK(probe.other_removed);
        CHECK_FALSE(world.entity_is_valid(entity));
        CHECK_FALSE(world.entity_is_valid(other));
        CHECK(world.counters_get().entities == 1); // the root
        CHECK(world.counters_get().components == 0);

        // Each slot is in the free list one time.
        const zecs::Entity first = world.entity_add();
        const zecs::Entity second = world.entity_add();
        const zecs::Entity third = world.entity_add();
        CHECK(zecs::entity_to_index(first) != zecs::entity_to_index(second));
        CHECK(zecs::entity_to_index(third) > zecs::entity_to_index(other));
    }

    SUBCASE("an entity added in a handler does not take the slot of the removed one")
    {
        zecs::World world("remove-add");
        const zecs::Entity entity = world.entity_add();

        REQUIRE(world.comp_add<test::Position>(entity, 1, 2) != nullptr);
        REQUIRE(world.comp_add<test::Velocity>(entity, 3, 4) != nullptr);

        EntityRemoveProbe probe;
        world.comp_on_removed<test::Position>().Connect(&probe, &EntityRemoveProbe::on_removed_add_entity);

        CHECK(world.entity_remove(entity));

        CHECK(probe.valid_in_handler);
        CHECK(world.entity_is_valid(probe.added));
        CHECK(zecs::entity_to_index(probe.added) != zecs::entity_to_index(entity));
        CHECK(world.comp_get<test::Velocity>(probe.added) == nullptr);

        // The slot is free only after all components are removed.
        const zecs::Entity reused = world.entity_add();
        CHECK(zecs::entity_to_index(reused) == zecs::entity_to_index(entity));
        CHECK(reused != entity);
    }

    SUBCASE("comp_remove leaves the entity valid in a handler")
    {
        zecs::World world("remove-comp");
        const zecs::Entity entity = world.entity_add();

        REQUIRE(world.comp_add<test::Position>(entity, 1, 2) != nullptr);

        EntityRemoveProbe probe;
        world.comp_on_removed<test::Position>().Connect(&probe, &EntityRemoveProbe::on_removed_add_entity);

        CHECK(world.comp_remove<test::Position>(entity));
        CHECK(probe.valid_in_handler);
        CHECK(world.entity_is_valid(entity));
    }
}

// The contract of the handlers lets them change components of other types.
// A handler that changes its own pool stops on an assert, so it has no test.
TEST_CASE("signals: a pool knows when its handlers are running")
{
    struct Probe
    {
        void on_signal(zecs::World& world, zecs::Entity)
        {
            running = world.comp_pool_get<test::Position>()->signal_is_running();
            other_running = world.comp_pool_get_or_add<test::Velocity>()->signal_is_running();
        }

        bool running = false;
        bool other_running = true;
    };

    zecs::World world;
    const zecs::Entity entity = world.entity_add();

    Probe probe;
    world.comp_on_added<test::Position>().Connect(&probe, &Probe::on_signal);
    world.comp_add<test::Position>(entity);

#ifdef ZECS_DEBUG
    CHECK(probe.running);
#else
    CHECK_FALSE(probe.running);
#endif
    CHECK_FALSE(probe.other_running);
    CHECK_FALSE(world.comp_pool_get<test::Position>()->signal_is_running());
}
