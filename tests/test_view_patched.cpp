#include "doctest/doctest.h"

#include "test_common_api.h"

namespace
{

struct PatchedCompsFixture
{
    PatchedCompsFixture()
        : world("patched")
    {
        first = world.entity_add();
        second = world.entity_add();
        third = world.entity_add();
        without_position = world.entity_add();

        world.comp_add<test::Position>(first, 10, 10);
        world.comp_add<test::Position>(second, 20, 20);
        world.comp_add<test::Position>(third, 30, 30);
        world.comp_add<test::Velocity>(without_position, 1, 1);

        tick_after_setup = world.tick_get();
    }

    zecs::TickContext since_setup() const
    {
        return zecs::TickContext(tick_after_setup);
    }

    zecs::World world;
    zecs::Entity first = zecs::Entity_INVALID;
    zecs::Entity second = zecs::Entity_INVALID;
    zecs::Entity third = zecs::Entity_INVALID;
    zecs::Entity without_position = zecs::Entity_INVALID;
    zecs::GTick tick_after_setup = 0;
};

} // namespace

// Covers snapshot semantics for patched-component views:
// empty snapshots, inclusive boundaries, repatch order, removals and newly added components.
TEST_CASE("view_patched")
{
    PatchedCompsFixture fx;

    SUBCASE("snapshot with no later changes is empty")
    {
        const zecs::TickContext ctx = fx.since_setup();

        CHECK(test::collect_entities(fx.world.view_patched_comps<test::Position>(ctx)).empty());
        CHECK(test::collect_entities(fx.world.view_patched_comps(zecs::globalUid<test::Position>, ctx)).empty());
    }

    SUBCASE("patch boundary is inclusive for exact since tick")
    {
        fx.world.comp_patch<test::Position>(fx.first);

        auto* pool = fx.world.comp_pool_get<test::Position>();
        REQUIRE(pool != nullptr);

        const zecs::GTick patch_tick = pool->patch_get(fx.first);

        const auto exact_entities = test::collect_entities(fx.world.view_patched_comps<test::Position>(zecs::TickContext(patch_tick)));
        const auto after_entities = test::collect_entities(fx.world.view_patched_comps<test::Position>(zecs::TickContext(patch_tick + 1)));

        REQUIRE(exact_entities.size() == 1);
        CHECK(exact_entities[0] == fx.first);
        CHECK(after_entities.empty());
    }

    SUBCASE("repatching same entity does not duplicate it and moves it to the front")
    {
        const zecs::TickContext ctx = fx.since_setup();

        fx.world.comp_patch<test::Position>(fx.first);
        fx.world.comp_patch<test::Position>(fx.second);
        fx.world.comp_patch<test::Position>(fx.first);

        const auto entities = test::collect_entities(fx.world.view_patched_comps<test::Position>(ctx));

        REQUIRE(entities.size() == 2);
        CHECK(entities[0] == fx.first);
        CHECK(entities[1] == fx.second);
    }

    SUBCASE("removing a patched component drops it from patched view")
    {
        const zecs::TickContext ctx = fx.since_setup();

        fx.world.comp_patch<test::Position>(fx.first);
        fx.world.comp_patch<test::Position>(fx.second);
        CHECK(fx.world.comp_remove<test::Position>(fx.second));

        const auto entities = test::collect_entities(fx.world.view_patched_comps<test::Position>(ctx));

        REQUIRE(entities.size() == 1);
        CHECK(entities[0] == fx.first);
    }

    SUBCASE("patching entity without that component does not create entries")
    {
        const zecs::TickContext ctx = fx.since_setup();

        fx.world.comp_patch<test::Position>(fx.without_position);

        CHECK(test::collect_entities(fx.world.view_patched_comps<test::Position>(ctx)).empty());
    }

    SUBCASE("a patch by a pointer takes the type from the pointer")
    {
        const zecs::TickContext ctx = fx.since_setup();

        test::Position* position = fx.world.comp_get<test::Position>(fx.first);
        REQUIRE(position != nullptr);

        fx.world.comp_patch(fx.first, position);

        const auto entities = test::collect_entities(fx.world.view_patched_comps<test::Position>(ctx));
        REQUIRE(entities.size() == 1);
        CHECK(entities[0] == fx.first);
    }

    SUBCASE("newly added component can be repatched and still appears once")
    {
        const zecs::TickContext ctx = fx.since_setup();
        const zecs::Entity added = fx.world.entity_add();

        fx.world.comp_add<test::Position>(added, 99, 99);
        fx.world.comp_patch<test::Position>(added);
        fx.world.comp_patch<test::Position>(fx.third);

        const auto entities = test::collect_entities(fx.world.view_patched_comps<test::Position>(ctx));

        REQUIRE(entities.size() == 2);
        CHECK(entities[0] == fx.third);
        CHECK(entities[1] == added);
    }

    SUBCASE("view goes from the newest tick to the oldest after add, patch, remove and re-add")
    {
        const zecs::Entity added = fx.world.entity_add();

        fx.world.comp_patch<test::Position>(fx.first);
        fx.world.comp_remove<test::Position>(fx.second);
        fx.world.comp_add<test::Position>(added, 40, 40);       // takes the free slot
        fx.world.comp_add<test::Position>(fx.second, 50, 50);   // takes a new slot

        std::vector<zecs::Entity> entities;
        std::vector<zecs::GTick> ticks;
        auto view = fx.world.view_patched_comps<test::Position>(zecs::TickContext(0));
        for (auto it = view.begin(); it != view.end(); ++it)
        {
            entities.push_back(*it);
            ticks.push_back(it.tick());
        }

        const std::vector<zecs::Entity> expected{ fx.second, added, fx.first, fx.third };
        CHECK(entities == expected);

        for (size_t i = 1; i < ticks.size(); ++i)
            CHECK(ticks[i - 1] > ticks[i]);
    }
}
