#include "doctest/doctest.h"

#include "test_common_api.h"

#include <algorithm>
#include <vector>

namespace
{

// Ten entities owning Position, all patched after the snapshot in index order,
// so the patched view walks them newest-first: entities[9] .. entities[0].
struct MutationFixture
{
    static constexpr int count = 10;

    MutationFixture()
        : world("patched-mutation")
    {
        for (int i = 0; i < count; ++i)
        {
            entities.push_back(world.entity_add());
            world.comp_add<test::Position>(entities.back(), i, -i);
        }

        snapshot = zecs::TickContext(world.tick_get());

        for (zecs::Entity e : entities)
            world.comp_patch<test::Position>(e);
    }

    zecs::World world;
    std::vector<zecs::Entity> entities;
    zecs::TickContext snapshot;
};

bool contains(const std::vector<zecs::Entity>& haystack, zecs::Entity needle)
{
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

} // namespace

// Regression for known_issues #2: PatchIt used to follow links[head].next, which
// dense_rem repurposed as free-list linkage, so removing the visited component
// stranded the iterator in the free-list (seen=1 instead of 1000 / garbage).
TEST_CASE("patch view: removing current element during iteration")
{
    MutationFixture fx;

    std::vector<zecs::Entity> seen;
    int safety = 0;
    for (zecs::Entity e : fx.world.view_patched_comps<test::Position>(fx.snapshot))
    {
        CHECK(e != zecs::Entity_INVALID);
        CHECK(fx.world.comp_remove<test::Position>(e));
        seen.push_back(e);

        REQUIRE(++safety <= MutationFixture::count);
    }

    CHECK(seen.size() == MutationFixture::count);
    for (zecs::Entity e : fx.entities)
        CHECK(contains(seen, e));

    CHECK(test::collect_entities(fx.world.view_patched_comps<test::Position>(fx.snapshot)).empty());
}

// Removing the element the iterator is about to visit must skip it cleanly.
TEST_CASE("patch view: removing next element during iteration")
{
    MutationFixture fx;

    std::vector<zecs::Entity> seen;
    int safety = 0;
    for (zecs::Entity e : fx.world.view_patched_comps<test::Position>(fx.snapshot))
    {
        CHECK(e != zecs::Entity_INVALID);
        seen.push_back(e);

        if (seen.size() == 1)
        {
            // Iteration is recency-ordered: the head is entities[9], its
            // successor in the patch list is entities[8].
            CHECK(e == fx.entities[9]);
            CHECK(fx.world.comp_remove<test::Position>(fx.entities[8]));
        }

        REQUIRE(++safety <= MutationFixture::count);
    }

    CHECK(seen.size() == MutationFixture::count - 1);
    CHECK(seen.front() == fx.entities[9]);
    CHECK_FALSE(contains(seen, fx.entities[8]));
}

// Chained removal (current + its successor, e.g. via a re-entrant on_removed
// handler) must not land the iterator on a dead slot: known_issues #14 yielded
// Entity_INVALID here.
TEST_CASE("patch view: removing current and successor during iteration")
{
    MutationFixture fx;

    std::vector<zecs::Entity> seen;
    int safety = 0;
    for (zecs::Entity e : fx.world.view_patched_comps<test::Position>(fx.snapshot))
    {
        CHECK(e != zecs::Entity_INVALID);
        seen.push_back(e);

        if (seen.size() == 1)
        {
            CHECK(e == fx.entities[9]);
            CHECK(fx.world.comp_remove<test::Position>(fx.entities[9]));
            CHECK(fx.world.comp_remove<test::Position>(fx.entities[8]));
        }

        REQUIRE(++safety <= MutationFixture::count);
    }

    CHECK(seen.size() == MutationFixture::count - 1);
    CHECK_FALSE(contains(seen, fx.entities[8]));
    CHECK(contains(seen, fx.entities[0]));
    CHECK(contains(seen, fx.entities[7]));
}

// The freed slot is immediately reused by a new component: the iterator must
// stay memory-safe and keep walking live nodes. The recycled node re-enters the
// patch list at the head — ahead of the in-flight iterator — so this traversal
// does not visit it, but a fresh view must.
TEST_CASE("patch view: slot reuse during iteration")
{
    MutationFixture fx;

    std::vector<zecs::Entity> seen;
    zecs::Entity recycled = zecs::Entity_INVALID;
    int safety = 0;
    for (zecs::Entity e : fx.world.view_patched_comps<test::Position>(fx.snapshot))
    {
        CHECK(e != zecs::Entity_INVALID);
        seen.push_back(e);

        if (seen.size() == 1)
        {
            CHECK(fx.world.comp_remove<test::Position>(fx.entities[9]));
            recycled = fx.world.entity_add();
            CHECK(fx.world.comp_add<test::Position>(recycled, 99, 99) != nullptr);
        }

        REQUIRE(++safety <= MutationFixture::count + 1);
    }

    CHECK(seen.size() == MutationFixture::count);
    CHECK(contains(seen, fx.entities[8]));
    CHECK_FALSE(contains(seen, recycled));

    const auto fresh = test::collect_entities(fx.world.view_patched_comps<test::Position>(fx.snapshot));
    CHECK(fresh.size() == MutationFixture::count);
    CHECK(contains(fresh, recycled));
}

// Documented contract: patching a not-yet-visited node moves it to the list
// head, so the ongoing iteration skips it. Must stay memory-safe and terminate.
TEST_CASE("patch view: patching a future element during iteration skips it")
{
    MutationFixture fx;

    std::vector<zecs::Entity> seen;
    int safety = 0;
    for (zecs::Entity e : fx.world.view_patched_comps<test::Position>(fx.snapshot))
    {
        CHECK(e != zecs::Entity_INVALID);
        seen.push_back(e);

        if (seen.size() == 1)
        {
            CHECK(e == fx.entities[9]);
            fx.world.comp_patch<test::Position>(fx.entities[0]);
        }

        REQUIRE(++safety <= MutationFixture::count);
    }

    CHECK(seen.size() == MutationFixture::count - 1);
    CHECK(seen.front() == fx.entities[9]);
    CHECK_FALSE(contains(seen, fx.entities[0]));
}
