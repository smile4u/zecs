#include "doctest/doctest.h"

#include "test_common_api.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{

// A family of unique empty component types used to create a wide runtime ECS surface.
// The stress test interacts with them through runtime GUid APIs instead of typed sugar.
template<std::size_t Index>
struct StressComp
{
};

constexpr std::size_t component_type_count = 250;
constexpr std::size_t initial_entity_count = 95000;
constexpr std::size_t late_entity_count = 5000;
constexpr std::size_t total_entity_count = initial_entity_count + late_entity_count;
constexpr std::size_t component_mask_word_count = (component_type_count + 63) / 64;

using ComponentMask = std::array<std::uint64_t, component_mask_word_count>;

// Build the runtime GUID table once so the test can address 250 component types uniformly.
template<std::size_t... Indices>
std::array<zecs::GUid, sizeof...(Indices)> make_component_guids(std::index_sequence<Indices...>)
{
    return { zecs::globalUid<StressComp<Indices>>... };
}

// Force creation of all pools up front. This removes lazy-creation noise from the churn
// phases and makes the stress test focus on add/remove behavior under load.
template<std::size_t... Indices>
void ensure_component_pools(zecs::World& world, std::index_sequence<Indices...>)
{
    (world.comp_pool_get_or_add<StressComp<Indices>>(), ...);
}

// Compact bit-mask storage for the expected component state of each entity.
// The test maintains this model in parallel with the ECS world and compares them later.
bool component_has(const ComponentMask& mask, std::size_t component_index)
{
    const std::size_t word_index = component_index / 64;
    const std::size_t bit_index = component_index % 64;
    return (mask[word_index] & (std::uint64_t(1) << bit_index)) != 0;
}

void component_set(ComponentMask& mask, std::size_t component_index)
{
    const std::size_t word_index = component_index / 64;
    const std::size_t bit_index = component_index % 64;
    mask[word_index] |= (std::uint64_t(1) << bit_index);
}

void component_reset(ComponentMask& mask, std::size_t component_index)
{
    const std::size_t word_index = component_index / 64;
    const std::size_t bit_index = component_index % 64;
    mask[word_index] &= ~(std::uint64_t(1) << bit_index);
}

bool should_add_initial_component(std::size_t entity_index, std::size_t component_index)
{
    // Rule 1 spreads candidates with a cheap linear congruential pattern.
    // Rule 2 perturbs the distribution with an xor-based mix so neighboring
    // entities don't keep selecting almost the same component slices.
    // Rule 3 guarantees that each entity owns at least one deterministic
    // "personal" component tied to its index.
    return ((entity_index + component_index * 7) % 23 == 0)
        || (((entity_index * 13) ^ (component_index * 17)) % 97 == 0)
        || (component_index == (entity_index % component_type_count));
}

bool should_add_late_component(std::size_t entity_index, std::size_t component_index)
{
    // Late entities use a different family of coefficients so their population
    // pattern doesn't mirror phase 1 and therefore exercises hole-heavy worlds
    // with a second distinct layout of occupied component pools.
    return ((entity_index + component_index * 11) % 19 == 0)
        || (((entity_index * 29) + component_index * 31) % 89 == 0)
        || (component_index == ((entity_index * 3) % component_type_count));
}

} // namespace

// Large deterministic churn test over 100k entities and 250 component types:
// validates runtime add/remove paths, pool integrity and final world/view consistency.
TEST_CASE("stress_churn")
{
    zecs::World world("stress");

    // Pre-create every pool so the heavy churn below stresses sparse/dense bookkeeping,
    // runtime GUID lookup, and removal paths instead of mostly measuring lazy pool setup.
    ensure_component_pools(world, std::make_index_sequence<component_type_count>{});

    const auto component_guids = make_component_guids(std::make_index_sequence<component_type_count>{});

    // `entities` stores the actual ECS handles returned by the world.
    // The parallel arrays below store the expected model that we will validate against.
    std::vector<zecs::Entity> entities;
    entities.reserve(total_entity_count);

    std::vector<bool> entity_alive(total_entity_count, false);
    std::vector<ComponentMask> entity_components(total_entity_count);
    std::array<std::size_t, component_type_count> component_counts = {};

    // These flags help turn unexpected runtime API failures into explicit test failures
    // while still letting the scenario run to completion and report all broken invariants.
    bool add_failure = false;
    bool remove_failure = false;

    auto add_entity = [&](std::size_t entity_index) {
        entities.push_back(world.entity_add());
        entity_alive[entity_index] = true;
    };

    auto add_component = [&](std::size_t entity_index, std::size_t component_index) {
        // Ignore operations on dead entities and duplicate adds in the expected model.
        if (!entity_alive[entity_index])
            return;

        ComponentMask& mask = entity_components[entity_index];
        if (component_has(mask, component_index))
            return;

        // The real ECS operation is mirrored into the expected mask/counters only on success.
        void* comp = world.comp_add(entities[entity_index], component_guids[component_index]);
        if (comp == nullptr)
        {
            add_failure = true;
            return;
        }

        component_set(mask, component_index);
        component_counts[component_index]++;
    };

    auto remove_component = [&](std::size_t entity_index, std::size_t component_index) {
        // Ignore removals from dead entities and from components that are absent in the model.
        if (!entity_alive[entity_index])
            return;

        ComponentMask& mask = entity_components[entity_index];
        if (!component_has(mask, component_index))
            return;

        // Remove from ECS and then update the model so later validations can compare them.
        if (!world.comp_remove(entities[entity_index], component_guids[component_index]))
        {
            remove_failure = true;
            return;
        }

        component_reset(mask, component_index);
        component_counts[component_index]--;
    };

    auto remove_entity = [&](std::size_t entity_index) {
        // Entity removal is the heaviest path: it must invalidate the entity and clear
        // every component it owned from every pool.
        if (!entity_alive[entity_index])
            return;

        world.entity_remove(entities[entity_index]);
        entity_alive[entity_index] = false;

        ComponentMask& mask = entity_components[entity_index];
        for (std::size_t component_index = 0; component_index < component_type_count; ++component_index)
        {
            if (!component_has(mask, component_index))
                continue;

            component_reset(mask, component_index);
            component_counts[component_index]--;
        }
    };

    // Phase 1: create the first 95k entities and give each of them a deterministic,
    // irregular initial component distribution across all 250 component types.
    for (std::size_t entity_index = 0; entity_index < initial_entity_count; ++entity_index)
    {
        add_entity(entity_index);

        for (std::size_t component_index = 0; component_index < component_type_count; ++component_index)
        {
            if (should_add_initial_component(entity_index, component_index))
                add_component(entity_index, component_index);
        }
    }

    // Phase 2: churn the initial population by mixing extra adds and targeted removals.
    // The formulas intentionally overlap to create fragmented sparse/dense structures.
    for (std::size_t entity_index = 0; entity_index < initial_entity_count; ++entity_index)
    {
        // `43 + 11` and `67 + 23` are just two unrelated affine maps chosen to
        // hit different component indices for the same entity and avoid a small
        // repeating cycle across the 250 component types.
        add_component(entity_index, (entity_index * 43 + 11) % component_type_count);
        add_component(entity_index, (entity_index * 67 + 23) % component_type_count);

        // Periods `4`, `6` and `8` deliberately overlap so some entities see
        // several mutations in one pass. The target-component formulas are again
        // different affine maps to make removals/adds land in different pools.
        if ((entity_index % 4) == 0)
            remove_component(entity_index, (entity_index * 17 + 3) % component_type_count);
        if ((entity_index % 6) == 0)
            remove_component(entity_index, (entity_index * 29 + 7) % component_type_count);
        if ((entity_index % 8) == 0)
            add_component(entity_index, (entity_index * 71 + 19) % component_type_count);
    }

    // Phase 3: remove a large subset of the initial entities. This creates holes in the
    // entity array and forces every component pool to clean up many sparse references.
    for (std::size_t entity_index = 0; entity_index < initial_entity_count; ++entity_index)
    {
        // `9` and `17` are coprime enough for this purpose: together they remove
        // a broad, irregular subset of entities instead of a simple contiguous chunk.
        if ((entity_index % 9) == 0 || (entity_index % 17) == 0)
            remove_entity(entity_index);
    }

    // Phase 4: add 5k new entities after the world already contains many holes, then
    // populate them with a different deterministic pattern of component additions/removals.
    for (std::size_t entity_index = initial_entity_count; entity_index < total_entity_count; ++entity_index)
    {
        add_entity(entity_index);

        for (std::size_t component_index = 0; component_index < component_type_count; ++component_index)
        {
            if (should_add_late_component(entity_index, component_index))
                add_component(entity_index, component_index);
        }

        // Late-entity churn uses different coefficients from phase 2 so the new
        // 5k entities don't mirror the component choices of the original 95k.
        add_component(entity_index, (entity_index * 37 + 5) % component_type_count);
        add_component(entity_index, (entity_index * 79 + 13) % component_type_count);

        // Period `5` guarantees a regular trickle of removals among late arrivals.
        if ((entity_index % 5) == 0)
            remove_component(entity_index, (entity_index * 41 + 2) % component_type_count);
    }

    // Phase 5: global mixed churn across the full 100k entity index space. Some entities
    // lose components, some gain them, and some are deleted entirely after previous edits.
    for (std::size_t entity_index = 0; entity_index < total_entity_count; ++entity_index)
    {
        if (!entity_alive[entity_index])
            continue;

        // `13 + 29` and `97 + 31` are another pair of unrelated maps used in the
        // final sweep so that remove/add operations keep shuffling across many pools.
        remove_component(entity_index, (entity_index * 13 + 29) % component_type_count);
        add_component(entity_index, (entity_index * 97 + 31) % component_type_count);

        // Branch periods `27`, `14`, `21` intentionally split the surviving
        // entities into overlapping but different cohorts: some are deleted,
        // some lose one more component, and some gain one more component.
        if ((entity_index % 27) == 0)
            remove_entity(entity_index);
        else if ((entity_index % 14) == 0)
            remove_component(entity_index, (entity_index * 59 + 17) % component_type_count);
        else if ((entity_index % 21) == 0)
            add_component(entity_index, (entity_index * 101 + 47) % component_type_count);
    }

    // Sanity check: runtime add/remove APIs should not have reported unexpected failures.
    CHECK_FALSE(add_failure);
    CHECK_FALSE(remove_failure);

    // Every one of the 250 component types should have a materialized pool by now.
    std::size_t pool_count = 0;
    for (zecs::CompPool2Base* pool : world.view_pools())
    {
        CHECK(pool != nullptr);
        pool_count++;
    }
    CHECK(pool_count == component_type_count);

    // Reconstruct the expected live entity order. Entities go in the order of their
    // index in the world entity storage. A new entity can take the index of a removed
    // one, so this isn't the order of creation.
    std::vector<zecs::Entity> expected_live_entities;
    expected_live_entities.reserve(total_entity_count + 1);
    expected_live_entities.push_back(world.root());

    for (std::size_t entity_index = 0; entity_index < total_entity_count; ++entity_index)
    {
        const zecs::Entity entity = entities[entity_index];
        CHECK(world.entity_is_valid(entity) == entity_alive[entity_index]);

        if (entity_alive[entity_index])
            expected_live_entities.push_back(entity);
    }

    std::sort(expected_live_entities.begin(), expected_live_entities.end(), [](zecs::Entity a, zecs::Entity b)
    {
        return zecs::entity_to_index(a) < zecs::entity_to_index(b);
    });

    const auto live_entities = test::collect_entities(world.view_entities());
    CHECK(live_entities == expected_live_entities);

    // Final validation: for each of the 250 component types, collect the expected entity set
    // from the model and compare it with the runtime view exposed by the ECS world.
    // Component views are compared as sorted sets because dense-slot reuse under heavy churn
    // doesn't guarantee a stable insertion-order traversal.
    for (std::size_t component_index = 0; component_index < component_type_count; ++component_index)
    {
        std::vector<zecs::Entity> expected_component_entities;
        expected_component_entities.reserve(component_counts[component_index]);

        for (std::size_t entity_index = 0; entity_index < total_entity_count; ++entity_index)
        {
            if (entity_alive[entity_index] && component_has(entity_components[entity_index], component_index))
                expected_component_entities.push_back(entities[entity_index]);
        }

        const auto actual_component_entities = test::collect_entities(world.view_comps(component_guids[component_index]));
        auto sorted_actual_component_entities = actual_component_entities;
        auto sorted_expected_component_entities = expected_component_entities;

        std::sort(sorted_actual_component_entities.begin(), sorted_actual_component_entities.end());
        std::sort(sorted_expected_component_entities.begin(), sorted_expected_component_entities.end());

        CHECK(sorted_actual_component_entities == sorted_expected_component_entities);
        CHECK(actual_component_entities.size() == component_counts[component_index]);
    }
}
