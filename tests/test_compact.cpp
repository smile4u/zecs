#include "doctest/doctest.h"

#include "test_common_api.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{

// Can be moved, but not copied.
struct MoveOnly
{
    std::string text;
    std::unique_ptr<int> number;
};

// Can't be moved at all.
struct Pinned
{
    static constexpr bool stable_pointers = true;

    explicit Pinned(int value_) : value(value_) {}
    Pinned(Pinned&&) = delete;

    int value = 0;
};

// Counts the calls of the component signals.
struct SignalCounter
{
    void on_signal(zecs::World&, zecs::Entity) { ++calls; }

    int calls = 0;
};

// Reads the size of sparse[], which the pool hides.
struct PoolProbe : zecs::CompPool2Base
{
    static size_t sparse_size(const zecs::CompPool2Base& pool) { return (pool.*(&PoolProbe::sparse)).size(); }
};

// More entities than one page of Position holds, so pages are freed too.
constexpr int entity_count = 10000;

struct CompactFixture
{
    CompactFixture()
        : world("compact")
    {
        for (int i = 0; i < entity_count; ++i)
        {
            const zecs::Entity e = world.entity_add();
            entities.push_back(e);

            world.comp_add<test::Position>(e, i, -i);
            world.comp_add<MoveOnly>(e, std::to_string(i), std::make_unique<int>(i));
            world.comp_add<test::Tag>(e);
            alive[e] = i;
        }
    }

    // Leaves the components only on every `step`-th entity.
    void remove_all_but_every(int step)
    {
        for (int i = 0; i < entity_count; ++i)
        {
            if (i % step == 0)
                continue;

            const zecs::Entity e = entities[i];
            world.comp_remove<test::Position>(e);
            world.comp_remove<MoveOnly>(e);
            world.comp_remove<test::Tag>(e);
            alive.erase(e);
        }
    }

    // Every living component is in the views and has its own value.
    void check_alive()
    {
        size_t seen = 0;
        for (zecs::Entity e : world.view_comp_group<test::Position, MoveOnly, test::Tag>())
        {
            ++seen;
            REQUIRE(alive.count(e) == 1);

            const int value = alive[e];
            CHECK(world.comp_get<test::Position>(e)->x == value);
            CHECK(world.comp_get<test::Position>(e)->y == -value);
            CHECK(world.comp_get<MoveOnly>(e)->text == std::to_string(value));
            CHECK(*world.comp_get<MoveOnly>(e)->number == value);
        }

        CHECK(seen == alive.size());
        CHECK(test::collect_entities(world.view_comps<test::Position>()).size() == alive.size());
        CHECK(world.comp_pool_get<test::Position>()->size() == alive.size());

        for (zecs::Entity e : entities)
            CHECK((world.comp_get<test::Position>(e) != nullptr) == (alive.count(e) == 1));
    }

    zecs::World world;
    std::vector<zecs::Entity> entities;
    std::map<zecs::Entity, int> alive;
};

} // namespace

// compact(): components keep their values, the patch order stays,
// memory is freed, and the pool works after it.
TEST_CASE("compact")
{
    CompactFixture fx;

    SUBCASE("components keep their values when most of the pool is removed")
    {
        fx.remove_all_but_every(7);
        fx.world.compact();
        fx.check_alive();
    }

    SUBCASE("pool without removed components does not move anything")
    {
        const auto* first = fx.world.comp_get<test::Position>(fx.entities.front());
        const auto* last = fx.world.comp_get<test::Position>(fx.entities.back());

        fx.world.compact();

        CHECK(fx.world.comp_get<test::Position>(fx.entities.front()) == first);
        CHECK(fx.world.comp_get<test::Position>(fx.entities.back()) == last);
        fx.check_alive();
    }

    SUBCASE("pool with all components removed is empty and can be filled again")
    {
        fx.alive.clear();
        for (zecs::Entity e : fx.entities)
        {
            fx.world.comp_remove<test::Position>(e);
            fx.world.comp_remove<MoveOnly>(e);
            fx.world.comp_remove<test::Tag>(e);
        }

        fx.world.compact();
        fx.check_alive();

        for (int i = 0; i < 100; ++i)
        {
            const zecs::Entity e = fx.entities[i];
            fx.world.comp_add<test::Position>(e, i, -i);
            fx.world.comp_add<MoveOnly>(e, std::to_string(i), std::make_unique<int>(i));
            fx.world.comp_add<test::Tag>(e);
            fx.alive[e] = i;
        }

        fx.check_alive();
    }

    SUBCASE("patch view keeps its order")
    {
        fx.remove_all_but_every(3);
        fx.world.comp_patch<test::Position>(fx.entities[9000]);
        fx.world.comp_patch<test::Position>(fx.entities[3]);
        fx.world.comp_patch<test::Position>(fx.entities[6000]);

        const auto before = test::collect_entities(fx.world.view_patched_comps<test::Position>(zecs::TickContext(0)));
        fx.world.compact();
        const auto after = test::collect_entities(fx.world.view_patched_comps<test::Position>(zecs::TickContext(0)));

        REQUIRE(after.size() == fx.alive.size());
        CHECK(after == before);
        CHECK(after[0] == fx.entities[6000]);
    }

    SUBCASE("pool works after compact: add, remove and compact again")
    {
        fx.remove_all_but_every(5);
        fx.world.compact();

        for (int i = 0; i < 500; ++i)
        {
            const zecs::Entity e = fx.world.entity_add();
            fx.entities.push_back(e);
            fx.world.comp_add<test::Position>(e, 100000 + i, -100000 - i);
            fx.world.comp_add<MoveOnly>(e, std::to_string(100000 + i), std::make_unique<int>(100000 + i));
            fx.world.comp_add<test::Tag>(e);
            fx.alive[e] = 100000 + i;

            if (i % 3 == 0)
            {
                fx.world.comp_remove<test::Position>(e);
                fx.world.comp_remove<MoveOnly>(e);
                fx.world.comp_remove<test::Tag>(e);
                fx.alive.erase(e);
            }
        }

        fx.check_alive();
        fx.world.compact();
        fx.check_alive();
    }

    SUBCASE("component that can not be moved stays in its place")
    {
        std::vector<const Pinned*> pointers;
        for (int i = 0; i < 100; ++i)
            pointers.push_back(fx.world.comp_add<Pinned>(fx.entities[i], i));

        for (int i = 0; i < 100; i += 2)
            fx.world.comp_remove<Pinned>(fx.entities[i]);

        fx.world.compact();

        for (int i = 1; i < 100; i += 2)
        {
            CHECK(fx.world.comp_get<Pinned>(fx.entities[i]) == pointers[i]);
            CHECK(pointers[i]->value == i);
        }
    }

    SUBCASE("component that can not be moved: empty slots at the end are cut")
    {
        std::vector<const Pinned*> pointers;
        for (int i = 0; i < 100; ++i)
            pointers.push_back(fx.world.comp_add<Pinned>(fx.entities[i], i));

        for (int i = 50; i < 100; ++i)
            fx.world.comp_remove<Pinned>(fx.entities[i]);

        fx.world.compact();

        CHECK(fx.world.comp_pool_get<Pinned>()->size() == 50);
        CHECK(test::collect_entities(fx.world.view_comps<Pinned>()).size() == 50);
        for (int i = 0; i < 50; ++i)
            CHECK(fx.world.comp_get<Pinned>(fx.entities[i]) == pointers[i]);

        // The pool takes new components after it.
        CHECK(fx.world.comp_add<Pinned>(fx.entities[70], 70)->value == 70);
        CHECK(fx.world.comp_pool_get<Pinned>()->size() == 51);
    }

    SUBCASE("moving a component does not call the added and removed signals")
    {
        SignalCounter counter;
        fx.world.comp_on_added<test::Position>().Connect(&counter, &SignalCounter::on_signal);
        fx.world.comp_on_removed<test::Position>().Connect(&counter, &SignalCounter::on_signal);

        fx.remove_all_but_every(7);
        const int calls_before = counter.calls;

        fx.world.compact();

        CHECK(counter.calls == calls_before);
        fx.check_alive();
    }

    SUBCASE("ID of a removed entity stays invalid after compact")
    {
        const zecs::Entity old = fx.entities[10];
        fx.world.entity_remove(old);
        fx.alive.erase(old);
        fx.remove_all_but_every(2);

        fx.world.compact();

        // The new entity takes the index of the removed one.
        const zecs::Entity fresh = fx.world.entity_add();
        fx.world.comp_add<test::Position>(fresh, 1, 2);

        CHECK(fx.world.comp_get<test::Position>(old) == nullptr);
        CHECK(fx.world.comp_get<test::Position>(fresh)->x == 1);
    }

    SUBCASE("sparse table ends at the last entity that has the component")
    {
        const zecs::CompPool2Base& pool = *fx.world.comp_pool_get<test::Position>();
        const zecs::CompPool2Base& pinned = *fx.world.comp_pool_get_or_add<Pinned>();
        const size_t full_size = PoolProbe::sparse_size(pool);

        // Pinned can't be moved, but its sparse[] is cut too.
        fx.world.comp_add<Pinned>(fx.entities[5], 5);
        fx.world.comp_add<Pinned>(fx.entities.back(), 7);
        fx.world.comp_remove<Pinned>(fx.entities.back());

        // Only the first 100 entities keep the components.
        for (int i = 100; i < entity_count; ++i)
        {
            const zecs::Entity e = fx.entities[i];
            fx.world.comp_remove<test::Position>(e);
            fx.world.comp_remove<MoveOnly>(e);
            fx.world.comp_remove<test::Tag>(e);
            fx.alive.erase(e);
        }

        fx.world.compact();

        CHECK(PoolProbe::sparse_size(pool) < full_size);
        CHECK(PoolProbe::sparse_size(pool) == zecs::entity_to_index(fx.entities[99]) + 1);
        CHECK(PoolProbe::sparse_size(pinned) == zecs::entity_to_index(fx.entities[5]) + 1);
        fx.check_alive();

        // An entity above the new end gets the component again.
        const zecs::Entity last = fx.entities.back();
        fx.world.comp_add<test::Position>(last, 1, -1);
        fx.world.comp_add<MoveOnly>(last, std::to_string(1), std::make_unique<int>(1));
        fx.world.comp_add<test::Tag>(last);
        fx.alive[last] = 1;
        fx.check_alive();
        CHECK(fx.world.comp_get<Pinned>(fx.entities[5])->value == 5);
    }

    SUBCASE("pool with all components removed has an empty sparse table")
    {
        fx.alive.clear();
        for (zecs::Entity e : fx.entities)
        {
            fx.world.comp_remove<test::Position>(e);
            fx.world.comp_remove<MoveOnly>(e);
            fx.world.comp_remove<test::Tag>(e);
        }

        fx.world.compact();

        CHECK(PoolProbe::sparse_size(*fx.world.comp_pool_get<test::Position>()) == 0);
        fx.check_alive();
    }

    SUBCASE("one pool can be compacted through the runtime API")
    {
        fx.remove_all_but_every(4);

        zecs::CompPool2Base* pool = fx.world.comp_pool_get(zecs::globalUid<test::Position>);
        REQUIRE(pool != nullptr);
        pool->compact();

        fx.check_alive();
    }
}
