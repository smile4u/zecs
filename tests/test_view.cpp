#include "doctest/doctest.h"

#include "test_common_api.h"

#include <array>
#include <type_traits>

namespace
{

// Its pool keeps a removed slot empty, and a new component takes it again.
struct StablePosition
{
    static constexpr bool stable_pointers = true;

    int x = 0;
    int y = 0;
};

template<typename View>
constexpr bool view_has_stable_identity =
    !std::is_copy_constructible_v<View> && !std::is_move_constructible_v<View>
    && !std::is_copy_assignable_v<View> && !std::is_move_assignable_v<View>
    && std::is_copy_constructible_v<typename View::It> && std::is_move_constructible_v<typename View::It>
    && std::is_copy_assignable_v<typename View::It> && std::is_move_assignable_v<typename View::It>;

static_assert(view_has_stable_identity<zecs::ViewComps>);
static_assert(view_has_stable_identity<zecs::ViewCompGroup2<test::Position>>);
static_assert(view_has_stable_identity<zecs::ViewCompGroup2<test::Position, test::Velocity>>);
static_assert(view_has_stable_identity<zecs::ViewCompGroupRt>);
static_assert(view_has_stable_identity<zecs::ViewEntities>);
static_assert(view_has_stable_identity<zecs::ViewEvents<test::DamageEvent>>);
static_assert(view_has_stable_identity<zecs::ViewPatchedComps>);
static_assert(view_has_stable_identity<zecs::ViewPools>);

} // namespace

// Covers the main zecs views on populated and empty worlds:
// component views, group views, stale views, mutation during traversal and membership cleanup.
TEST_CASE("view_api")
{
    test::ViewWorldFixture fx;

    SUBCASE("views over missing pools and deques behave as empty ranges")
    {
        // This block mirrors the invalid-view coverage from entt. The goal is to
        // lock in that zecs returns harmless empty ranges when the corresponding
        // pool or event deque has never been created.
        zecs::World world("empty-views");
        const zecs::TickContext ctx(world.tick_get());

        CHECK(test::collect_pools(world.view_pools()).empty());
        CHECK(test::collect_entities(world.view_comps<test::Position>()).empty());
        CHECK(test::collect_entities(world.view_comps(zecs::globalUid<test::Position>)).empty());
        CHECK(test::collect_entities(world.view_comp_group<test::Position, test::Velocity>()).empty());
        {
            const std::array<zecs::GUid, 2> guids = {
                zecs::globalUid<test::Position>,
                zecs::globalUid<test::Velocity>,
            };
            CHECK(test::collect_entities(world.view_comp_group(guids.data(), guids.size())).empty());
        }
        CHECK(test::collect_entities(world.view_patched_comps<test::Position>(ctx)).empty());
        CHECK(test::collect_entities(world.view_patched_comps(zecs::globalUid<test::Position>, ctx)).empty());
        CHECK(world.view_events<test::DamageEvent>(ctx).empty());
    }

    SUBCASE("views captured before pool creation stay stale while fresh views see new pools")
    {
        // Unlike entt registry views, zecs views are thin wrappers around the pool
        // pointer available at construction time. Capturing a view before the pool
        // exists should therefore remain empty, while a freshly requested view
        // should immediately observe the newly created pool.
        zecs::World world("stale-view");
        const zecs::Entity entity = world.entity_add();

        auto stale_typed = world.view_comps<test::Position>();
        auto stale_runtime = world.view_comps(zecs::globalUid<test::Position>);

        CHECK(test::collect_entities(stale_typed).empty());
        CHECK(test::collect_entities(stale_runtime).empty());

        REQUIRE(world.comp_add<test::Position>(entity, 4, 8) != nullptr);

        CHECK(test::collect_entities(stale_typed).empty());
        CHECK(test::collect_entities(stale_runtime).empty());

        const auto fresh_typed = test::collect_entities(world.view_comps<test::Position>());
        const auto fresh_runtime = test::collect_entities(world.view_comps(zecs::globalUid<test::Position>));

        REQUIRE(fresh_typed.size() == 1);
        CHECK(fresh_typed[0] == entity);
        CHECK(fresh_runtime == fresh_typed);
    }

    SUBCASE("view_comps iterates entities that own a component")
    {
        const auto entities = test::collect_entities(fx.world.view_comps<test::Position>());

        CHECK(test::sorted(entities) == test::sorted({ fx.position_only, fx.in_group_a, fx.in_group_b, fx.patch_first, fx.patch_second }));
    }

    SUBCASE("view_comps runtime guid overload matches typed view")
    {
        const auto typed_entities = test::collect_entities(fx.world.view_comps<test::Position>());
        const auto runtime_entities = test::collect_entities(fx.world.view_comps(zecs::globalUid<test::Position>));

        CHECK(runtime_entities == typed_entities);
    }

    SUBCASE("view_entities supports removing current entity during traversal")
    {
        std::vector<zecs::Entity> visited;

        for (zecs::Entity entity : fx.world.view_entities())
        {
            visited.push_back(entity);

            if (entity != fx.world.root())
                fx.world.entity_remove(entity);
        }

        REQUIRE(visited.size() == 8);
        CHECK(visited[0] == fx.world.root());
        CHECK(visited[1] == fx.position_only);
        CHECK(visited[2] == fx.in_group_a);
        CHECK(visited[3] == fx.in_group_b);
        CHECK(visited[4] == fx.velocity_only);
        CHECK(visited[5] == fx.tag_only);
        CHECK(visited[6] == fx.patch_first);
        CHECK(visited[7] == fx.patch_second);

        const auto remaining = test::collect_entities(fx.world.view_entities());
        REQUIRE(remaining.size() == 1);
        CHECK(remaining[0] == fx.world.root());
    }

    SUBCASE("view_comps supports removing current component during traversal")
    {
        std::vector<zecs::Entity> visited;

        for (zecs::Entity entity : fx.world.view_comps<test::Position>())
        {
            visited.push_back(entity);
            CHECK(fx.world.comp_remove<test::Position>(entity));
        }

        CHECK(test::sorted(visited) == test::sorted({ fx.position_only, fx.in_group_a, fx.in_group_b, fx.patch_first, fx.patch_second }));
        CHECK(test::collect_entities(fx.world.view_comps<test::Position>()).empty());
    }

    SUBCASE("view_comps can remove an entity that the pass visited already")
    {
        // Position has no stable pointers. So the body must not remove
        // the component of an entity that the pass didn't visit yet
        // (see stable_pointers in config.h). It can remove the component
        // of an entity that the pass visited already.
        std::vector<zecs::Entity> visited;

        for (zecs::Entity entity : fx.world.view_comps<test::Position>())
        {
            visited.push_back(entity);

            if (visited.size() == 3)
                fx.world.entity_remove(visited[0]);
        }

        CHECK(test::sorted(visited) == test::sorted({ fx.position_only, fx.in_group_a, fx.in_group_b, fx.patch_first, fx.patch_second }));
        CHECK(fx.world.comp_get<test::Position>(visited[0]) == nullptr);
        CHECK(test::collect_entities(fx.world.view_comps<test::Position>()).size() == 4);
    }

    SUBCASE("view_comp_group returns intersection of component sets")
    {
        const auto entities = test::collect_entities(fx.world.view_comp_group<test::Position, test::Velocity>());

        CHECK(test::sorted(entities) == test::sorted({ fx.in_group_a, fx.in_group_b }));
    }

    SUBCASE("view_comp_group is empty when pools exist but do not overlap")
    {
        auto view = fx.world.view_comp_group<test::Velocity, test::Tag>();
        CHECK(view.empty());
        CHECK(test::collect_entities(view).empty());
    }

    SUBCASE("view_comp_group runtime guid overload matches typed group view")
    {
        const std::array<zecs::GUid, 2> guids = {
            zecs::globalUid<test::Position>,
            zecs::globalUid<test::Velocity>,
        };

        const auto typed_entities = test::collect_entities(fx.world.view_comp_group<test::Position, test::Velocity>());
        const auto runtime_entities = test::collect_entities(fx.world.view_comp_group(guids.data(), guids.size()));

        CHECK(runtime_entities == typed_entities);
    }

    SUBCASE("group view handles empty tags mixed with regular components")
    {
        // Empty marker components often take a separate code path in ECS storage.
        // This verifies that intersection logic stays correct when one side of the
        // group is a tag-only component.
        const zecs::Entity tagged_position = fx.world.entity_add();
        REQUIRE(fx.world.comp_add<test::Position>(tagged_position, 9, 9) != nullptr);
        REQUIRE(fx.world.comp_add<test::Tag>(tagged_position) != nullptr);

        const auto entities = test::collect_entities(fx.world.view_comp_group<test::Position, test::Tag>());

        REQUIRE(entities.size() == 1);
        CHECK(entities[0] == tagged_position);
    }

    SUBCASE("view_comp_group supports removing current entity during traversal")
    {
        std::vector<zecs::Entity> visited;

        for (zecs::Entity entity : fx.world.view_comp_group<test::Position, test::Velocity>())
        {
            visited.push_back(entity);
            fx.world.entity_remove(entity);
        }

        CHECK(test::sorted(visited) == test::sorted({ fx.in_group_a, fx.in_group_b }));
        CHECK(test::collect_entities(fx.world.view_comp_group<test::Position, test::Velocity>()).empty());
    }

    SUBCASE("runtime group view supports removing current entity during traversal")
    {
        const std::array<zecs::GUid, 2> guids = {
            zecs::globalUid<test::Position>,
            zecs::globalUid<test::Velocity>,
        };

        std::vector<zecs::Entity> visited;

        for (zecs::Entity entity : fx.world.view_comp_group(guids.data(), guids.size()))
        {
            visited.push_back(entity);
            fx.world.entity_remove(entity);
        }

        CHECK(test::sorted(visited) == test::sorted({ fx.in_group_a, fx.in_group_b }));
        CHECK(test::collect_entities(fx.world.view_comp_group(guids.data(), guids.size())).empty());
    }

    SUBCASE("view_comp_group supports removing current component during traversal")
    {
        std::vector<zecs::Entity> visited;

        for (zecs::Entity entity : fx.world.view_comp_group<test::Position, test::Velocity>())
        {
            visited.push_back(entity);
            CHECK(fx.world.comp_remove<test::Velocity>(entity));
        }

        CHECK(test::sorted(visited) == test::sorted({ fx.in_group_a, fx.in_group_b }));
        CHECK(test::collect_entities(fx.world.view_comp_group<test::Position, test::Velocity>()).empty());
    }

    SUBCASE("view_comp_group can remove an entity that the pass visited already")
    {
        // The pass goes over Velocity, and it has no stable pointers. So the
        // body must not remove the Velocity of an entity that the pass
        // didn't visit yet. It can remove it from a visited entity.
        std::vector<zecs::Entity> visited;

        for (zecs::Entity entity : fx.world.view_comp_group<test::Position, test::Velocity>())
        {
            visited.push_back(entity);

            if (visited.size() == 2)
                fx.world.entity_remove(visited[0]);
        }

        CHECK(test::sorted(visited) == test::sorted({ fx.in_group_a, fx.in_group_b }));
        CHECK_FALSE(fx.world.entity_is_valid(visited[0]));
        CHECK(test::collect_entities(fx.world.view_comp_group<test::Position, test::Velocity>())
            == std::vector<zecs::Entity>{ visited[1] });
    }

    SUBCASE("view_comp_group skips a future entity that loses a component of another pool")
    {
        // The pass goes over Velocity: it has fewer components than Position.
        // The components of Position can be removed from any entity.
        REQUIRE(fx.world.comp_pool_get<test::Velocity>()->size() < fx.world.comp_pool_get<test::Position>()->size());

        std::vector<zecs::Entity> visited;
        zecs::Entity future = zecs::Entity_INVALID;

        for (zecs::Entity entity : fx.world.view_comp_group<test::Position, test::Velocity>())
        {
            visited.push_back(entity);

            if (future == zecs::Entity_INVALID)
            {
                future = (entity == fx.in_group_a) ? fx.in_group_b : fx.in_group_a;
                CHECK(fx.world.comp_remove<test::Position>(future));
            }
        }

        REQUIRE(visited.size() == 1);
        CHECK(visited[0] != future);
        CHECK(fx.world.comp_get<test::Position>(future) == nullptr);
        CHECK(test::collect_entities(fx.world.view_comp_group<test::Position, test::Velocity>()) == visited);
    }

    SUBCASE("views rebuild membership correctly after remove re-add and entity replacement")
    {
        // This is the closest zecs analogue to entt's clean view/group tests.
        // The intent is to ensure that intersection membership isn't cached
        // incorrectly across remove, re-add and full entity replacement.
        zecs::World world("clean-membership");
        const zecs::Entity original = world.entity_add();

        REQUIRE(world.comp_add<test::Position>(original, 1, 1) != nullptr);
        REQUIRE(world.comp_add<test::Velocity>(original, 2, 2) != nullptr);

        auto group_entities = test::collect_entities(world.view_comp_group<test::Position, test::Velocity>());
        REQUIRE(group_entities.size() == 1);
        CHECK(group_entities[0] == original);

        CHECK(world.comp_remove<test::Velocity>(original));
        CHECK(test::collect_entities(world.view_comp_group<test::Position, test::Velocity>()).empty());

        REQUIRE(world.comp_add<test::Velocity>(original, 3, 3) != nullptr);
        group_entities = test::collect_entities(world.view_comp_group<test::Position, test::Velocity>());
        REQUIRE(group_entities.size() == 1);
        CHECK(group_entities[0] == original);

        world.entity_remove(original);
        CHECK(test::collect_entities(world.view_comp_group<test::Position, test::Velocity>()).empty());

        const zecs::Entity replacement = world.entity_add();
        REQUIRE(world.comp_add<test::Position>(replacement, 4, 4) != nullptr);
        REQUIRE(world.comp_add<test::Velocity>(replacement, 5, 5) != nullptr);

        group_entities = test::collect_entities(world.view_comp_group<test::Position, test::Velocity>());
        REQUIRE(group_entities.size() == 1);
        CHECK(group_entities[0] == replacement);
    }

    SUBCASE("view_comps does not visit components added to new entities during traversal")
    {
        // A pass starts from the last slot and goes to the first one.
        // A new component takes a slot at the end of the pool. The pass
        // has left the end already, so it doesn't visit the new component.
        zecs::World world("add-during-comps");
        const zecs::Entity e1 = world.entity_add();
        const zecs::Entity e2 = world.entity_add();
        const zecs::Entity e3 = world.entity_add();
        REQUIRE(world.comp_add<test::Position>(e1, 1, 1) != nullptr);
        REQUIRE(world.comp_add<test::Position>(e2, 2, 2) != nullptr);
        REQUIRE(world.comp_add<test::Position>(e3, 3, 3) != nullptr);

        std::vector<zecs::Entity> visited;
        bool added = false;
        for (zecs::Entity entity : world.view_comps<test::Position>())
        {
            visited.push_back(entity);
            if (!added)
            {
                added = true;
                const zecs::Entity ne = world.entity_add();
                REQUIRE(world.comp_add<test::Position>(ne, 9, 9) != nullptr);
            }
        }

        CHECK(test::sorted(visited) == test::sorted({ e1, e2, e3 }));
    }

    SUBCASE("view_comps with end() called on every step does not visit new slots")
    {
        // A loop that calls end() on every step must stop at the same place as
        // a range-for. New slots at the end of the pool aren't visited.
        zecs::World world("grow-during-comps");
        const zecs::Entity e1 = world.entity_add();
        const zecs::Entity e2 = world.entity_add();
        REQUIRE(world.comp_add<test::Position>(e1, 1, 1) != nullptr);
        REQUIRE(world.comp_add<test::Position>(e2, 2, 2) != nullptr);

        auto view = world.view_comps<test::Position>();
        std::vector<zecs::Entity> visited;
        for (auto it = view.begin(); it != view.end(); ++it)
        {
            visited.push_back(*it);
            if (visited.size() == 1)
            {
                const zecs::Entity removed = world.entity_add();
                const zecs::Entity kept = world.entity_add();
                REQUIRE(world.comp_add<test::Position>(removed, 8, 8) != nullptr);
                REQUIRE(world.comp_add<test::Position>(kept, 9, 9) != nullptr);
                REQUIRE(world.comp_remove<test::Position>(removed));
            }

            REQUIRE(visited.size() <= 2);
        }

        CHECK(test::sorted(visited) == test::sorted({ e1, e2 }));
        CHECK(test::collect_entities(view).size() == 3);
    }

    SUBCASE("view_comps visits components that reuse freed slots during traversal")
    {
        // The pool free list hands back a slot freed mid-pass; the reused slot is
        // a real (non-INVALID) dense entry, so the traversal picks up the new owner.
        // Only a pool with stable pointers keeps a freed slot in its place.
        // The pass goes from the last slot, so e1 is the one it hasn't reached.
        zecs::World world("reuse-during");
        const zecs::Entity e1 = world.entity_add();
        const zecs::Entity e2 = world.entity_add();
        const zecs::Entity e3 = world.entity_add();
        REQUIRE(world.comp_add<StablePosition>(e1, 1, 1) != nullptr);
        REQUIRE(world.comp_add<StablePosition>(e2, 2, 2) != nullptr);
        REQUIRE(world.comp_add<StablePosition>(e3, 3, 3) != nullptr);

        std::vector<zecs::Entity> visited;
        zecs::Entity ne = zecs::Entity_INVALID;
        bool done = false;
        for (zecs::Entity entity : world.view_comps<StablePosition>())
        {
            visited.push_back(entity);
            if (!done)
            {
                done = true;
                REQUIRE(world.comp_remove<StablePosition>(e1));
                ne = world.entity_add();
                REQUIRE(world.comp_add<StablePosition>(ne, 9, 9) != nullptr);
            }
        }

        REQUIRE(visited.size() == 3);
        CHECK(visited[0] == e3);
        CHECK(visited[1] == e2);
        CHECK(visited[2] == ne);
    }

    SUBCASE("group view picks up components added to a future entity during traversal")
    {
        // Group membership is evaluated on demand for each lead entity, so a
        // component added mid-pass to an entity still ahead in the lead pool is
        // observed when the traversal reaches it.
        // The pass goes from the last slot of Position: z, y, x.
        zecs::World world("group-add-during");
        const zecs::Entity x = world.entity_add();
        const zecs::Entity y = world.entity_add();
        const zecs::Entity z = world.entity_add();
        REQUIRE(world.comp_add<test::Position>(x, 1, 1) != nullptr);
        REQUIRE(world.comp_add<test::Position>(y, 2, 2) != nullptr);
        REQUIRE(world.comp_add<test::Position>(z, 3, 3) != nullptr);
        REQUIRE(world.comp_add<test::Velocity>(y, 20, 20) != nullptr);
        // Velocity pool stays >= Position pool so Position (index 0) leads the group.
        const zecs::Entity w = world.entity_add();
        const zecs::Entity v = world.entity_add();
        REQUIRE(world.comp_add<test::Velocity>(w, 30, 30) != nullptr);
        REQUIRE(world.comp_add<test::Velocity>(v, 40, 40) != nullptr);

        std::vector<zecs::Entity> visited;
        bool added = false;
        for (zecs::Entity entity : world.view_comp_group<test::Position, test::Velocity>())
        {
            visited.push_back(entity);
            if (!added)
            {
                added = true;
                REQUIRE(world.comp_add<test::Velocity>(x, 33, 33) != nullptr);
            }
        }

        REQUIRE(visited.size() == 2);
        CHECK(visited[0] == y);
        CHECK(visited[1] == x);
    }
}
