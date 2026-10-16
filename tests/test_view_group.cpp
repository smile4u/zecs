#include "doctest/doctest.h"

#include "test_common_api.h"

#include <array>
#include <utility>
#include <vector>

namespace
{

template<size_t Index>
struct GroupTag {};

struct GroupFixture
{
    static constexpr size_t count = zecs::ViewCompGroupRt::INLINE_CAPACITY + 2;

    GroupFixture()
    {
        first = world.entity_add();
        second = world.entity_add();
        partial = world.entity_add();
        populate(std::make_index_sequence<count>{});
        world.comp_add<GroupTag<0>>(partial);
    }

    template<size_t... Indices>
    void populate(std::index_sequence<Indices...>)
    {
        guids = {zecs::globalUid<GroupTag<Indices>>...};
        (world.comp_add<GroupTag<Indices>>(first), ...);
        (world.comp_add<GroupTag<Indices>>(second), ...);
    }

    zecs::World world;
    zecs::Entity first;
    zecs::Entity second;
    zecs::Entity partial;
    std::array<zecs::GUid, count> guids;
};

template<typename MakeView>
void check_iterator_semantics(GroupFixture& fx, MakeView make_view)
{
    using View = decltype(make_view(fx.world));

    SUBCASE("iterator equality is symmetric before and after exhaustion")
    {
        const auto view = make_view(fx.world);
        auto first = view.begin();
        auto second = first;
        const auto end = view.end();
        const typename View::It default_end;

        CHECK(first == second);
        CHECK(first != end);
        CHECK(end != first);
        CHECK(end == default_end);

        ++first;
        CHECK(first != second);
        CHECK(second != first);
        ++second;
        CHECK(first == second);

        ++first;
        CHECK(first == end);
        CHECK(end == first);
        CHECK(first == default_end);
        CHECK(first != second);
        ++second;
        CHECK(first == second);

        second = view.begin();
        CHECK(second != first);
        CHECK(*second == fx.first);
    }

    SUBCASE("exhausted snapshot differs from a new live slot at the old bound")
    {
        const auto view = make_view(fx.world);
        auto exhausted = view.begin();
        ++exhausted;
        ++exhausted;
        REQUIRE(exhausted == view.end());

        const auto added = fx.world.entity_add();
        for (auto guid : fx.guids)
            fx.world.comp_add(added, guid);

        auto current = view.begin();
        ++current;
        ++current;
        REQUIRE(current != view.end());
        CHECK(*current == added);
        CHECK(exhausted != current);
        CHECK(current != exhausted);
        CHECK(exhausted == view.end());

        ++current;
        CHECK(current == exhausted);
        CHECK(exhausted == current);
    }
}

template<typename MakeView>
void check_fresh_iteration(GroupFixture& fx, MakeView make_view)
{
    const auto view = make_view(fx.world);

    // Tag<1> leads. Removing Tag<0> forces begin() to recheck the first
    // candidate's membership instead of returning the old cached iterator.
    REQUIRE(fx.world.comp_remove<GroupTag<0>>(fx.first));
    CHECK(test::collect_entities(view) == std::vector<zecs::Entity>{fx.second});

    REQUIRE(fx.world.comp_remove<GroupTag<1>>(fx.second));
    CHECK(view.empty());

    fx.world.comp_add<GroupTag<0>>(fx.first);
    CHECK_FALSE(view.empty());
    CHECK(test::collect_entities(view) == std::vector<zecs::Entity>{fx.first});

    // Append two lead entries: one reuses a hole, the other extends dense.
    fx.world.comp_add<GroupTag<1>>(fx.second);
    fx.world.comp_add<GroupTag<1>>(fx.partial);
    const std::vector<zecs::Entity> expected = {fx.first, fx.second, fx.partial};
    CHECK(test::collect_entities(view) == expected);
}

template<typename MakeView>
void check_iteration_bound(GroupFixture& fx, MakeView make_view)
{
    const auto view = make_view(fx.world);
    std::vector<zecs::Entity> visited;
    std::vector<zecs::Entity> added;
    auto visit = [&](zecs::Entity e) {
        visited.push_back(e);
        if (visited.size() == 1)
        {
            // Grow well beyond the initial tiny dense/sparse allocations.
            // Iterators must reload vector data after the callback returns.
            for (size_t i = 0; i < 4096; ++i)
            {
                const auto entity = fx.world.entity_add();
                added.push_back(entity);
                for (auto guid : fx.guids)
                    fx.world.comp_add(entity, guid);
            }
        }
    };

    SUBCASE("range for caches end")
    {
        for (auto e : view)
            visit(e);
    }

    SUBCASE("explicit loop queries end on every iteration")
    {
        for (auto it = view.begin(); it != view.end(); ++it)
            visit(*it);
    }

    const std::vector<zecs::Entity> expected_pass = {fx.first, fx.second};
    CHECK(visited == expected_pass);
    std::vector<zecs::Entity> expected_next_pass = {fx.first, fx.second};
    expected_next_pass.insert(expected_next_pass.end(), added.begin(), added.end());
    CHECK(test::collect_entities(view) == expected_next_pass);
}

template<typename MakeView>
void check_mixed_mutation(GroupFixture& fx, MakeView make_view)
{
    for (size_t i = 1; i < fx.guids.size(); ++i)
        fx.world.comp_add(fx.partial, fx.guids[i]);
    const auto view = make_view(fx.world);
    auto remove = [&](zecs::Entity e) {
        for (auto guid : fx.guids)
            REQUIRE(fx.world.comp_remove(e, guid));
    };
    std::vector<zecs::Entity> visited;
    zecs::Entity replacement = zecs::Entity_INVALID;
    for (auto it = view.begin(); it != view.end(); ++it)
    {
        const auto e = *it;
        visited.push_back(e);
        REQUIRE(visited.size() <= 2);
        if (e == fx.first)
        {
            remove(e);
            remove(fx.second);
            replacement = fx.world.entity_add();
            for (auto guid : fx.guids)
                fx.world.comp_add(replacement, guid);
        }
        else
        {
            REQUIRE(e == replacement);
            remove(e);
            remove(fx.partial);
        }
    }
    // The replacement reuses the next slot, the third slot is removed before
    // reaching it, and deleting the current slot never invalidates the cursor.
    CHECK(visited == std::vector<zecs::Entity>{fx.first, replacement});
    CHECK(view.empty());
    for (auto guid : fx.guids)
        fx.world.comp_add(fx.partial, guid);
    CHECK(test::collect_entities(view) == std::vector<zecs::Entity>{fx.partial});
}

} // namespace

TEST_CASE("group view iterator semantics")
{
    GroupFixture fx;

    SUBCASE("typed single pool")
    {
        check_iterator_semantics(fx, [](zecs::World& world) {
            return world.view_comp_group<GroupTag<1>>();
        });
    }

    SUBCASE("typed")
    {
        check_iterator_semantics(fx, [](zecs::World& world) {
            return world.view_comp_group<GroupTag<0>, GroupTag<1>>();
        });
    }

    SUBCASE("runtime inline capacity")
    {
        check_iterator_semantics(fx, [&](zecs::World& world) {
            return world.view_comp_group(fx.guids.data(), zecs::ViewCompGroupRt::INLINE_CAPACITY);
        });
    }

    SUBCASE("runtime heap fallback")
    {
        check_iterator_semantics(fx, [&](zecs::World& world) {
            return world.view_comp_group(fx.guids.data(), zecs::ViewCompGroupRt::INLINE_CAPACITY + 1);
        });
    }
}

TEST_CASE("typed group checks every pool with a non-first lead")
{
    GroupFixture fx;
    using View = zecs::ViewCompGroup2<GroupTag<0>, GroupTag<1>, GroupTag<2>>;

    SUBCASE("both non-leading membership checks filter candidates")
    {
        // Tag<2> is the smallest pool, so construction swaps it into slot 0.
        fx.world.comp_add<GroupTag<1>>(fx.partial);
        const auto view = fx.world.view_comp_group<GroupTag<0>, GroupTag<1>, GroupTag<2>>();

        REQUIRE(fx.world.comp_remove<GroupTag<0>>(fx.first));
        CHECK_FALSE(view.has(fx.first));
        CHECK(view.has(fx.second));
        CHECK(test::collect_entities(view) == std::vector<zecs::Entity>{fx.second});

        REQUIRE(fx.world.comp_remove<GroupTag<1>>(fx.second));
        CHECK_FALSE(view.has(fx.second));
        CHECK(view.empty());

        fx.world.comp_add<GroupTag<0>>(fx.first);
        CHECK(view.has(fx.first));
        CHECK(test::collect_entities(view) == std::vector<zecs::Entity>{fx.first});
    }

    SUBCASE("a missing pool at any position makes the typed range empty")
    {
        const View::Pools all_pools = {
            fx.world.comp_pool_get<GroupTag<0>>(),
            fx.world.comp_pool_get<GroupTag<1>>(),
            fx.world.comp_pool_get<GroupTag<2>>()
        };
        for (size_t missing = 0; missing < all_pools.size(); ++missing)
        {
            auto pools = all_pools;
            pools[missing] = nullptr;
            const View view(pools);
            CHECK(view.empty());
            CHECK_FALSE(view.has(fx.first));
            CHECK(test::collect_entities(view).empty());
        }
    }
}

TEST_CASE("group view refreshes iteration between passes")
{
    GroupFixture fx;

    SUBCASE("typed")
    {
        check_fresh_iteration(fx, [](zecs::World& world) {
            return world.view_comp_group<GroupTag<0>, GroupTag<1>>();
        });
    }

    SUBCASE("runtime")
    {
        check_fresh_iteration(fx, [&](zecs::World& world) {
            return world.view_comp_group(fx.guids.data(), 2);
        });
    }
}

TEST_CASE("group view keeps traversal bounds while the lead pool grows")
{
    GroupFixture fx;

    SUBCASE("typed single pool")
    {
        check_iteration_bound(fx, [](zecs::World& world) {
            return world.view_comp_group<GroupTag<1>>();
        });
    }

    SUBCASE("runtime single pool")
    {
        check_iteration_bound(fx, [&](zecs::World& world) {
            return world.view_comp_group(&fx.guids[1], 1);
        });
    }

    SUBCASE("typed")
    {
        check_iteration_bound(fx, [](zecs::World& world) {
            return world.view_comp_group<GroupTag<0>, GroupTag<1>>();
        });
    }

    SUBCASE("runtime inline")
    {
        check_iteration_bound(fx, [&](zecs::World& world) {
            return world.view_comp_group(fx.guids.data(), 2);
        });
    }

    SUBCASE("runtime heap")
    {
        check_iteration_bound(fx, [&](zecs::World& world) {
            return world.view_comp_group(fx.guids.data(), fx.guids.size());
        });
    }
}

TEST_CASE("group view supports mixed removal and slot reuse in one pass")
{
    GroupFixture fx;

    SUBCASE("typed single pool")
    {
        check_mixed_mutation(fx, [](zecs::World& world) {
            return world.view_comp_group<GroupTag<1>>();
        });
    }
    SUBCASE("typed two pools")
    {
        check_mixed_mutation(fx, [](zecs::World& world) {
            return world.view_comp_group<GroupTag<0>, GroupTag<1>>();
        });
    }
    SUBCASE("typed three pools")
    {
        check_mixed_mutation(fx, [](zecs::World& world) {
            return world.view_comp_group<GroupTag<0>, GroupTag<1>, GroupTag<2>>();
        });
    }
    SUBCASE("runtime inline")
    {
        check_mixed_mutation(fx, [&](zecs::World& world) {
            return world.view_comp_group(fx.guids.data(), zecs::ViewCompGroupRt::INLINE_CAPACITY);
        });
    }
    SUBCASE("runtime heap")
    {
        check_mixed_mutation(fx, [&](zecs::World& world) {
            return world.view_comp_group(fx.guids.data(), fx.guids.size());
        });
    }
}

TEST_CASE("runtime group storage and empty ranges")
{
    GroupFixture fx;
    const std::vector<zecs::Entity> expected = {fx.first, fx.second};

    SUBCASE("zero pools form an empty range")
    {
        const auto view = fx.world.view_comp_group(nullptr, 0);
        CHECK(view.empty());
        CHECK_FALSE(view.has(fx.first));
        CHECK(test::collect_entities(view).empty());
        CHECK(zecs::ViewCompGroupRt(zecs::CompPoolsPacked{}).empty());
    }

    SUBCASE("one pool matches its component view")
    {
        CHECK(test::collect_entities(fx.world.view_comp_group(fx.guids.data(), 1))
            == test::collect_entities(fx.world.view_comps<GroupTag<0>>()));
        CHECK(test::collect_entities(fx.world.view_comp_group<GroupTag<0>>())
            == test::collect_entities(fx.world.view_comps<GroupTag<0>>()));
    }

    SUBCASE("entity zero is live even though dense slot zero marks end")
    {
        zecs::World world;
        world.comp_add<GroupTag<0>>(world.root());
        const zecs::GUid guid = zecs::globalUid<GroupTag<0>>;
        const std::vector<zecs::Entity> expected = {world.root()};
        CHECK(test::collect_entities(world.view_comp_group<GroupTag<0>>()) == expected);
        CHECK(test::collect_entities(world.view_comp_group(&guid, 1)) == expected);
    }

    SUBCASE("missing pools at any position form empty ranges")
    {
        for (size_t count : {zecs::ViewCompGroupRt::INLINE_CAPACITY, GroupFixture::count})
        {
            for (size_t missing = 0; missing < count; ++missing)
            {
                auto guids = fx.guids;
                guids[missing] = zecs::globalUid<GroupTag<100>>;
                const auto view = fx.world.view_comp_group(guids.data(), count);
                CHECK(view.empty());
                CHECK_FALSE(view.has(fx.first));
                CHECK(test::collect_entities(view).empty());
            }
        }
    }

    SUBCASE("vector constructor supports inline and heap storage")
    {
        for (size_t count : {zecs::ViewCompGroupRt::INLINE_CAPACITY, GroupFixture::count})
        {
            zecs::CompPoolsPacked pools;
            for (size_t i = 0; i < count; ++i)
                pools.push_back(fx.world.comp_pool_get(fx.guids[i]));
            const zecs::ViewCompGroupRt view(std::move(pools));
            CHECK(test::collect_entities(view) == expected);
        }
    }
}
