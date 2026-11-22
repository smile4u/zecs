#include "doctest/doctest.h"

#include "test_common_api.h"

#include <type_traits>

namespace
{
// Empty types that still need their constructor or destructor.
struct EmptyWithCtor
{
    EmptyWithCtor() { ++made; }
    explicit EmptyWithCtor(int count) { made += count; }

    static inline int made = 0;
};

struct EmptyWithDtor
{
    ~EmptyWithDtor() { ++gone; }

    static inline int gone = 0;
};

// Components of different size, to check the size of a pool page.
struct Big
{
    int value = 0;
    char pad[156] = {};
};

struct Huge
{
    char pad[100000] = {};
};

struct OwnPage
{
    static constexpr size_t page_size = 8;

    int value = 0;
    char pad[156] = {};
};

// A type that we can not change. Its page size is set by a specialization.
struct ForeignPage
{
    int value = 0;
};

// Components that ask for pointers that stay valid.
struct OwnStable
{
    static constexpr bool stable_pointers = true;

    int value = 0;
};

struct ForeignStable
{
    int value = 0;
};
} // namespace

template<>
struct zecs::CompConfig<ForeignStable> : zecs::CompConfigDefault<ForeignStable>
{
    static constexpr bool stable_pointers = true;
};

template<>
struct zecs::CompConfig<ForeignPage> : zecs::CompConfigDefault<ForeignPage>
{
    static constexpr size_t page_size = 16;
};

// Pointers are not stable by default. A type sets it in itself
// or in a specialization, and the other settings keep their defaults.
static_assert(!zecs::CompConfig<test::Position>::stable_pointers);
static_assert(!zecs::CompConfig<test::Tag>::stable_pointers);
static_assert(zecs::CompConfig<OwnStable>::stable_pointers);
static_assert(zecs::CompConfig<ForeignStable>::stable_pointers);
static_assert(zecs::CompConfig<ForeignStable>::page_size == 4096);
static_assert(!zecs::CompConfig<ForeignPage>::stable_pointers);

// Covers typed and runtime component operations on a populated world:
// add/get/get_or_add/remove, singleton helpers, pool lifecycle and destruction behavior.
// Type numbers must be separate enum types (see guid.h).
static_assert(std::is_enum_v<zecs::CUidIndex>);
static_assert(std::is_enum_v<zecs::EUidIndex>);
static_assert(!std::is_same_v<zecs::CUidIndex, zecs::EUidIndex>);

static_assert(zecs::are_types_unique<int>);
static_assert(zecs::are_types_unique<int, float, char>);
static_assert(!zecs::are_types_unique<int, float, int>);
static_assert(!zecs::are_types_unique<int, int>);

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

        SUBCASE("get_or_add without arguments does not add a non default constructible component")
        {
            auto* pool = fx.world.comp_pool_get<test::NonDefault>();
            REQUIRE(pool != nullptr);

            zecs::GTick tick = 100;
            const auto res = pool->get_or_add(fx.get_or_add_entity, &tick);

            CHECK(res.first == nullptr);
            CHECK_FALSE(res.second);
            CHECK(tick == 100);
            CHECK_FALSE(pool->contains(fx.get_or_add_entity));
        }

        SUBCASE("add and patch that do nothing keep the world tick")
        {
            const zecs::Entity e = fx.get_or_add_entity;
            const zecs::GTick before = fx.world.tick_get();

            // The entity has no Position: nothing to patch.
            fx.world.comp_patch<test::Position>(e);
            fx.world.comp_patch<test::Position>(e, nullptr);
            fx.world.comp_patch(e, zecs::globalUid<test::Position>);
            CHECK(fx.world.tick_get() == before);

            // NonDefault can not be made without arguments.
            CHECK(fx.world.comp_add<test::NonDefault>(e) == nullptr);
            CHECK(fx.world.comp_add(e, zecs::globalUid<test::NonDefault>) == nullptr);
            CHECK(fx.world.tick_get() == before);

            // Real work still takes one tick each.
            REQUIRE(fx.world.comp_add<test::Position>(e, 1, 2) != nullptr);
            fx.world.comp_patch<test::Position>(e);
            CHECK(fx.world.tick_get() == before + 2);
        }

        SUBCASE("empty component with a constructor is constructed")
        {
            EmptyWithCtor::made = 0;
            const zecs::Entity first = fx.world.entity_add();
            const zecs::Entity second = fx.world.entity_add();

            auto* a = fx.world.comp_add<EmptyWithCtor>(first);
            CHECK(EmptyWithCtor::made == 1);

            auto* b = fx.world.comp_add<EmptyWithCtor>(second, 5);
            CHECK(EmptyWithCtor::made == 6);

            // It is a normal component: every entity has its own object.
            REQUIRE(a != nullptr);
            REQUIRE(b != nullptr);
            CHECK(a != b);
            CHECK(fx.world.comp_get<EmptyWithCtor>(first) == a);
        }

        SUBCASE("empty component with a destructor is destroyed")
        {
            EmptyWithDtor::gone = 0;
            {
                zecs::World world("empty-with-dtor");
                const zecs::Entity first = world.entity_add();
                const zecs::Entity second = world.entity_add();
                const zecs::Entity third = world.entity_add();
                REQUIRE(world.comp_add<EmptyWithDtor>(first) != nullptr);
                REQUIRE(world.comp_add<EmptyWithDtor>(second) != nullptr);
                REQUIRE(world.comp_add<EmptyWithDtor>(third) != nullptr);

                // The last component is removed every time, so no component
                // moves and no other object is destroyed.
                CHECK(world.comp_remove<EmptyWithDtor>(third));
                CHECK(EmptyWithDtor::gone == 1);

                world.entity_remove(second);
                CHECK(EmptyWithDtor::gone == 2);
            }

            // The world destroys the component that is still alive.
            CHECK(EmptyWithDtor::gone == 3);
        }

        SUBCASE("pool knows its component type")
        {
            const zecs::CompPool2Base* pool = fx.world.comp_pool_get(zecs::globalUid<test::Position>);
            REQUIRE(pool != nullptr);

            CHECK(pool == fx.world.comp_pool_get<test::Position>());
            CHECK(pool->guid == zecs::globalUid<test::Position>);
            CHECK(*pool->type == typeid(test::Position));
        }

        SUBCASE("pool page is about 64 KB and at most 4096 components")
        {
            CHECK(zecs::CompPool2<test::Position>::PAGE_SIZE == 4096);
            CHECK(zecs::CompPool2<Big>::PAGE_SIZE == 512);
            CHECK(zecs::CompPool2<Huge>::PAGE_SIZE == 1);
            CHECK(zecs::CompPool2<OwnPage>::PAGE_SIZE == 8);
            CHECK(zecs::CompPool2<ForeignPage>::PAGE_SIZE == 16);

            // Components keep their values over many pages.
            std::vector<zecs::Entity> entities;
            for (int i = 0; i < 1000; ++i)
            {
                entities.push_back(fx.world.entity_add());
                fx.world.comp_add<Big>(entities.back())->value = i;
                fx.world.comp_add<OwnPage>(entities.back())->value = -i;
            }

            for (int i = 0; i < 1000; ++i)
            {
                CHECK(fx.world.comp_get<Big>(entities[i])->value == i);
                CHECK(fx.world.comp_get<OwnPage>(entities[i])->value == -i);
            }
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
            CHECK(test::sorted(entities) == test::sorted({ fx.tag_entity, fx.get_or_add_entity }));

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

            // The removal can move the other component into the free slot.
            // A move makes one more object and destroys one more.
            CHECK(fx.world.comp_remove<test::TrackedLifetime>(fx.tracked_first));
            CHECK(test::TrackedLifetime::alive == 1);
            CHECK(test::TrackedLifetime::destroyed == test::TrackedLifetime::constructed - 1);
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
    CHECK(test::TrackedLifetime::constructed >= 2);
    CHECK(test::TrackedLifetime::destroyed == test::TrackedLifetime::constructed);
}
