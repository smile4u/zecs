#include "doctest/doctest.h"

#include "zecs/world.h"

namespace test_static_init
{

// These types are used only in this file, so nothing gives them
// an index before the constructor below.
struct First
{
    int value = 0;
};

struct Second
{
    int value = 0;
};

// A global object that uses components in its constructor. It can run
// before the program sets cuidIndex<First> and cuidIndex<Second>.
struct EarlyUser
{
    EarlyUser()
    {
        index_was_set = (zecs::cuidIndex<First> != zecs::CUidIndex_NONE);

        entity = world.entity_add();
        first = world.comp_add<First>(entity, 1);
        second = world.comp_add<Second>(entity, 2);

        first_found = (world.comp_get<First>(entity) == first);
        second_found = (world.comp_get<Second>(entity) == second);
        different_pools = (static_cast<void*>(world.comp_pool_get<First>()) != static_cast<void*>(world.comp_pool_get<Second>()));
    }

    zecs::World world;
    zecs::Entity entity = zecs::Entity_INVALID;
    First* first = nullptr;
    Second* second = nullptr;
    bool index_was_set = false;
    bool first_found = false;
    bool second_found = false;
    bool different_pools = false;
};

EarlyUser early_user;

template<typename View>
int count(View&& view)
{
    int result = 0;
    for (const zecs::Entity e : view)
    {
        (void)e;
        ++result;
    }
    return result;
}

} // namespace test_static_init

TEST_CASE("components work in the constructor of a global object")
{
    using namespace test_static_init;

    // The order of the start isn't fixed: on some compilers the index is set
    // before the constructor, and then this test checks the usual way.
    MESSAGE("the index was set before the constructor: " << early_user.index_was_set);

    CHECK(early_user.first_found);
    CHECK(early_user.second_found);
    CHECK(early_user.different_pools);

    // The same pools are found after the start of the program.
    zecs::World& world = early_user.world;
    CHECK(zecs::cuidIndex<First> != zecs::CUidIndex_NONE);
    REQUIRE(world.comp_get<First>(early_user.entity) == early_user.first);
    REQUIRE(world.comp_get<Second>(early_user.entity) == early_user.second);
    CHECK(early_user.first->value == 1);
    CHECK(early_user.second->value == 2);
    CHECK(world.counters_get().pools == 2);
}

// The test above can't choose the order of the start. This one puts 0 into
// the variables by hand: they look like before the start of the program.
TEST_CASE("components work while the type has no index in cuidIndex")
{
    struct Third
    {
        int value = 0;
    };

    struct Fourth
    {
        int value = 0;
    };

    const zecs::CUidIndex third_index = zecs::cuidIndex<Third>;
    const zecs::CUidIndex fourth_index = zecs::cuidIndex<Fourth>;
    REQUIRE(third_index != zecs::CUidIndex_NONE);
    REQUIRE(fourth_index != third_index);

    zecs::cuidIndex<Third> = zecs::CUidIndex_NONE;
    zecs::cuidIndex<Fourth> = zecs::CUidIndex_NONE;

    zecs::World world;
    const zecs::Entity entity = world.entity_add();

    // Nothing is found, and no pool is made by a search.
    CHECK(world.comp_get<Third>(entity) == nullptr);
    CHECK(world.counters_get().pools == 0);

    Third* third = world.comp_add<Third>(entity, 3);
    Fourth* fourth = world.comp_add<Fourth>(entity, 4);
    REQUIRE(third != nullptr);
    REQUIRE(fourth != nullptr);

    CHECK(world.comp_get<Third>(entity) == third);
    CHECK(world.comp_get<Fourth>(entity) == fourth);
    CHECK(world.comp_get_or_add<Third>(entity) == third);
    CHECK(world.counters_get().pools == 2);
    CHECK(test_static_init::count(world.view_comp_group<Third, Fourth>()) == 1);

    // The world gave the types the same indices as before.
    CHECK(zecs::cuidIndex<Third> == third_index);
    CHECK(zecs::cuidIndex<Fourth> == fourth_index);

    CHECK(world.comp_get<Third>(entity) == third);
    CHECK(world.comp_get<Fourth>(entity) == fourth);
    CHECK(world.comp_remove<Third>(entity));
    CHECK(world.counters_get().pools == 2);
    CHECK(world.counters_get().components == 1);
}
