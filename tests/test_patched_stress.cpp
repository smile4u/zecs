#include "doctest/doctest.h"

#include "test_common_api.h"

#include <algorithm>
#include <vector>

namespace
{

constexpr std::size_t initial_entity_count = 40000;
constexpr std::size_t late_entity_count = 10000;
constexpr std::size_t total_entity_count = initial_entity_count + late_entity_count;

bool should_add_initial_position(std::size_t entity_index)
{
    return ((entity_index % 3) == 0)
        || (((entity_index * 7) + 11) % 19 == 0)
        || ((entity_index % 64) == 1);
}

bool should_add_late_position(std::size_t entity_index)
{
    return ((entity_index % 4) == 0)
        || (((entity_index * 13) + 5) % 23 == 0)
        || ((entity_index % 27) == 2);
}

std::vector<zecs::Entity> collect_sorted_entities(const std::vector<zecs::Entity>& entities)
{
    auto sorted = entities;
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

} // namespace

// Stresses patched-component snapshots under large post-snapshot churn:
// patches, removals, entity deletion and typed/runtime patched views must stay aligned.
TEST_CASE("patched_stress")
{
    zecs::World world("patched-stress");
    const zecs::GUid position_guid = zecs::globalUid<test::Position>;

    // Keep real ECS handles separate from the parallel expected-state model.
    // The vectors below track which indexed entity is alive, currently owns Position,
    // and should still appear in the patched view for the chosen snapshot.
    std::vector<zecs::Entity> entities;
    entities.reserve(total_entity_count);

    std::vector<bool> entity_alive(total_entity_count, false);
    std::vector<bool> has_position(total_entity_count, false);
    std::vector<bool> patched_since_snapshot(total_entity_count, false);

    // The snapshot gate matters: mutations that happened before the captured tick
    // must not leak into the expected patched set built at the end of the test.
    bool snapshot_taken = false;
    bool add_failure = false;
    bool remove_failure = false;

    auto add_entity = [&](std::size_t entity_index) {
        entities.push_back(world.entity_add());
        entity_alive[entity_index] = true;
    };

    auto add_position = [&](std::size_t entity_index) {
        if (!entity_alive[entity_index] || has_position[entity_index])
            return;

        auto* position = world.comp_add<test::Position>(
            entities[entity_index],
            static_cast<int>(entity_index % 1000),
            -static_cast<int>(entity_index % 1000));

        if (position == nullptr)
        {
            add_failure = true;
            return;
        }

        // Adding a new component after the snapshot is itself a post-snapshot change,
        // so the entity must become visible in view_patched_comps().
        has_position[entity_index] = true;
        if (snapshot_taken)
            patched_since_snapshot[entity_index] = true;
    };

    auto patch_position_typed = [&](std::size_t entity_index) {
        if (!entity_alive[entity_index])
            return;

        world.comp_patch<test::Position>(entities[entity_index]);
        if (has_position[entity_index] && snapshot_taken)
            patched_since_snapshot[entity_index] = true;
    };

    auto patch_position_runtime = [&](std::size_t entity_index) {
        if (!entity_alive[entity_index])
            return;

        world.comp_patch(entities[entity_index], position_guid);
        if (has_position[entity_index] && snapshot_taken)
            patched_since_snapshot[entity_index] = true;
    };

    auto remove_position = [&](std::size_t entity_index) {
        if (!entity_alive[entity_index] || !has_position[entity_index])
            return;

        if (!world.comp_remove<test::Position>(entities[entity_index]))
        {
            remove_failure = true;
            return;
        }

        has_position[entity_index] = false;
        patched_since_snapshot[entity_index] = false;
    };

    auto remove_entity = [&](std::size_t entity_index) {
        // Removing the entity must also remove it from the expected patched result,
        // even if it had been patched earlier after the snapshot.
        if (!entity_alive[entity_index])
            return;

        world.entity_remove(entities[entity_index]);
        entity_alive[entity_index] = false;
        has_position[entity_index] = false;
        patched_since_snapshot[entity_index] = false;
    };

    // Phase 1: build a large initial population with a non-trivial Position layout.
    // Some entities are patched here on purpose to prove that pre-snapshot changes
    // are not returned later by the patched view.
    for (std::size_t entity_index = 0; entity_index < initial_entity_count; ++entity_index)
    {
        add_entity(entity_index);

        if (should_add_initial_position(entity_index))
            add_position(entity_index);

        if (has_position[entity_index] && ((entity_index % 5) == 0))
            patch_position_typed(entity_index);
    }

    const zecs::TickContext snapshot(world.tick_get());
    snapshot_taken = true;

    // Immediately after capturing the snapshot there were no post-snapshot changes yet,
    // so both typed and runtime patched views must be empty.
    CHECK(test::collect_entities(world.view_patched_comps<test::Position>(snapshot)).empty());

    // Phase 2: churn the initial population after the snapshot.
    // This mixes valid patches, no-op patches on entities without Position,
    // component removals, and whole-entity removals.
    for (std::size_t entity_index = 0; entity_index < initial_entity_count; ++entity_index)
    {
        if (has_position[entity_index] && (((entity_index % 3) == 0) || (((entity_index * 5) + 3) % 17 == 0)))
            patch_position_typed(entity_index);

        if (has_position[entity_index] && ((entity_index % 11) == 0))
            patch_position_runtime(entity_index);

        if (!has_position[entity_index] && ((entity_index % 13) == 0))
            patch_position_typed(entity_index);

        if (has_position[entity_index] && ((entity_index % 7) == 0))
            remove_position(entity_index);

        if ((entity_index % 23) == 0)
            remove_entity(entity_index);
    }

    // Phase 3: add a second wave of entities after the world already has holes.
    // New Position additions after the snapshot should count as patched entries,
    // unless the component or entity gets removed again later in the same run.
    for (std::size_t entity_index = initial_entity_count; entity_index < total_entity_count; ++entity_index)
    {
        add_entity(entity_index);

        if (should_add_late_position(entity_index))
            add_position(entity_index);

        if (has_position[entity_index] && ((entity_index % 5) == 0))
            patch_position_runtime(entity_index);

        if (has_position[entity_index] && ((entity_index % 9) == 0))
            remove_position(entity_index);

        if ((entity_index % 17) == 0)
            remove_entity(entity_index);
    }

    // Phase 4: a final global sweep across both old and new entities.
    // This creates overlap between add, patch, remove-component, and remove-entity
    // operations so the expected set only keeps entities that are still alive,
    // still own Position, and changed after the snapshot.
    for (std::size_t entity_index = 0; entity_index < total_entity_count; ++entity_index)
    {
        if (!entity_alive[entity_index])
            continue;

        if (!has_position[entity_index] && (((entity_index % 6) == 0) || (((entity_index * 3) + 1) % 41 == 0)))
            add_position(entity_index);
        else if (has_position[entity_index] && ((entity_index % 8) == 0))
            patch_position_runtime(entity_index);

        if (has_position[entity_index] && ((entity_index % 10) == 0))
            remove_position(entity_index);

        if ((entity_index % 37) == 0)
            remove_entity(entity_index);
    }

    CHECK_FALSE(add_failure);
    CHECK_FALSE(remove_failure);

    // Build the expected final answer from the parallel model. Only entities that
    // are still alive, still have Position, and changed after the snapshot belong
    // in the patched view.
    std::vector<zecs::Entity> expected_entities;
    expected_entities.reserve(total_entity_count);

    for (std::size_t entity_index = 0; entity_index < total_entity_count; ++entity_index)
    {
        if (entity_alive[entity_index] && has_position[entity_index] && patched_since_snapshot[entity_index])
            expected_entities.push_back(entities[entity_index]);
    }

    const auto typed_entities = test::collect_entities(world.view_patched_comps<test::Position>(snapshot));
    const auto runtime_entities = test::collect_entities(world.view_patched_comps(position_guid, snapshot));

    // Compare as sorted sets: patched order is intentionally recency-based,
    // while this stress test is validating membership under heavy churn.
    CHECK(collect_sorted_entities(typed_entities) == collect_sorted_entities(expected_entities));
    CHECK(collect_sorted_entities(runtime_entities) == collect_sorted_entities(expected_entities));
    CHECK(collect_sorted_entities(runtime_entities) == collect_sorted_entities(typed_entities));

    // A fresh snapshot taken after the churn should see no later changes yet.
    const zecs::TickContext after_churn(world.tick_get());
    CHECK(test::collect_entities(world.view_patched_comps<test::Position>(after_churn)).empty());
}
