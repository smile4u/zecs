#include "doctest/doctest.h"

#include "test_common_api.h"
#include "zecs/task.h"

#include <string>
#include <vector>

namespace
{

struct TaskLog
{
    std::vector<std::string> order;
    std::vector<zecs::GTick> init_since;
    std::vector<zecs::GTick> shutdown_since;
    std::vector<float> normal_dt;
    std::vector<zecs::GTick> normal_since;
    std::vector<float> fixed_dt;
    std::vector<zecs::GTick> fixed_since;

    int normal_skip_budget = 0;
    zecs::Entity normal_patch_target = zecs::Entity_INVALID;
    zecs::Entity fixed_patch_target = zecs::Entity_INVALID;
};

TaskLog* current_task_log = nullptr;

struct ScopedTaskLog
{
    explicit ScopedTaskLog(TaskLog& log) noexcept
    {
        current_task_log = &log;
    }

    ~ScopedTaskLog()
    {
        current_task_log = nullptr;
    }
};

struct TaskGraphShutdownGuard
{
    TaskGraphShutdownGuard(zecs::TaskGraph& graph_, zecs::World& world_) noexcept
        : graph(graph_)
        , world(world_)
    {
    }

    ~TaskGraphShutdownGuard()
    {
        if (graph.is_initialized())
            graph.shutdown(world);
    }

    zecs::TaskGraph& graph;
    zecs::World& world;
};

TaskLog& task_log()
{
    DOCTEST_REQUIRE(current_task_log != nullptr);
    return *current_task_log;
}

void task_init_first(zecs::World&, zecs::TickContext& ctx)
{
    auto& log = task_log();
    log.order.emplace_back("init/first");
    log.init_since.push_back(ctx.since);
}

void task_init_second(zecs::World&, zecs::TickContext& ctx)
{
    auto& log = task_log();
    log.order.emplace_back("init/second");
    log.init_since.push_back(ctx.since);
}

void task_step_normal(zecs::World& world, zecs::TickContext& ctx)
{
    auto& log = task_log();
    log.order.emplace_back("step/normal");
    log.normal_dt.push_back(ctx.dt);
    log.normal_since.push_back(ctx.since);

    if (log.normal_skip_budget > 0)
    {
        log.normal_skip_budget--;
        ctx.skip();
        return;
    }

    if (log.normal_patch_target != zecs::Entity_INVALID)
        world.comp_patch<test::Position>(log.normal_patch_target);
}

void task_step_fixed(zecs::World& world, zecs::TickContext& ctx)
{
    auto& log = task_log();
    log.order.emplace_back("step/fixed");
    log.fixed_dt.push_back(ctx.dt);
    log.fixed_since.push_back(ctx.since);

    if (log.fixed_patch_target != zecs::Entity_INVALID)
        world.comp_patch<test::Position>(log.fixed_patch_target);
}

void task_shutdown_first(zecs::World&, zecs::TickContext& ctx)
{
    auto& log = task_log();
    log.order.emplace_back("shutdown/first");
    log.shutdown_since.push_back(ctx.since);
}

void task_shutdown_second(zecs::World&, zecs::TickContext& ctx)
{
    auto& log = task_log();
    log.order.emplace_back("shutdown/second");
    log.shutdown_since.push_back(ctx.since);
}

void register_task_graph_tests()
{
    static bool registered = false;
    if (registered)
        return;

    auto* manager = zecs::TaskManager::instance();
    manager->add("test_task_graph/init_first", &task_init_first);
    manager->add("test_task_graph/init_second", &task_init_second);
    manager->add("test_task_graph/step_normal", &task_step_normal);
    manager->add("test_task_graph/step_fixed", &task_step_fixed);
    manager->add("test_task_graph/shutdown_first", &task_shutdown_first);
    manager->add("test_task_graph/shutdown_second", &task_shutdown_second);

    registered = true;
}

} // namespace

// Covers task graph execution order and TickContext semantics:
// init/shutdown ordering, dt propagation, fixed-step replay and skip behavior.
TEST_CASE("task_graph")
{
    register_task_graph_tests();

    SUBCASE("init and shutdown keep declared order and snapshot tick")
    {
        zecs::World world("task-init-shutdown");
        zecs::TaskGraph graph;
        TaskGraphShutdownGuard shutdown_guard(graph, world);

        TaskLog log;
        ScopedTaskLog scoped_log(log);

        graph.init_ids = { "test_task_graph/init_first", "test_task_graph/init_second" };
        graph.shutdown_ids = { "test_task_graph/shutdown_first", "test_task_graph/shutdown_second" };

        const zecs::GTick tick_before_init = world.tick_get();

        graph.init(world);
        CHECK(graph.is_initialized());
        graph.shutdown(world);
        CHECK_FALSE(graph.is_initialized());

        const std::vector<std::string> expected_order = {
            "init/first",
            "init/second",
            "shutdown/first",
            "shutdown/second"
        };

        CHECK(log.order == expected_order);

        REQUIRE(log.init_since.size() == 2);
        CHECK(log.init_since[0] == tick_before_init);
        CHECK(log.init_since[1] == tick_before_init);

        REQUIRE(log.shutdown_since.size() == 2);
        CHECK(log.shutdown_since[0] == tick_before_init);
        CHECK(log.shutdown_since[1] == tick_before_init);
    }

    SUBCASE("normal step propagates dt and advances since after successful run")
    {
        zecs::World world("task-normal");
        const zecs::Entity entity = world.entity_add();
        REQUIRE(world.comp_add<test::Position>(entity, 10, 20) != nullptr);

        zecs::TaskGraph graph;
        TaskGraphShutdownGuard shutdown_guard(graph, world);

        TaskLog log;
        log.normal_patch_target = entity;
        ScopedTaskLog scoped_log(log);

        graph.step_normal_ids = { "test_task_graph/step_normal" };
        graph.init(world);

        const zecs::GTick tick_after_setup = world.tick_get();

        graph.step(world, 0.5f);
        graph.step(world, 0.25f);

        REQUIRE(log.normal_dt.size() == 2);
        CHECK(log.normal_dt[0] == doctest::Approx(0.5f));
        CHECK(log.normal_dt[1] == doctest::Approx(0.25f));

        REQUIRE(log.normal_since.size() == 2);
        CHECK(log.normal_since[0] == 0);
        CHECK(log.normal_since[1] == tick_after_setup);

        CHECK(world.tick_get() == tick_after_setup + 2);
    }

    SUBCASE("fixed step uses accumulator and fixed dt for each replay")
    {
        zecs::World world("task-fixed");
        const zecs::Entity entity = world.entity_add();
        REQUIRE(world.comp_add<test::Position>(entity, 1, 1) != nullptr);

        zecs::TaskGraph graph(0.1f);
        TaskGraphShutdownGuard shutdown_guard(graph, world);

        TaskLog log;
        log.fixed_patch_target = entity;
        ScopedTaskLog scoped_log(log);

        graph.step_fixed_ids = { "test_task_graph/step_fixed" };
        graph.init(world);

        const zecs::GTick tick_after_setup = world.tick_get();

        // Use non-boundary values so the test validates accumulator semantics
        // instead of relying on float-equality around 0.1f.
        graph.step(world, 0.26f);
        graph.step(world, 0.14f);
        graph.step(world, 0.11f);

        REQUIRE(log.fixed_dt.size() == 5);
        for (float dt : log.fixed_dt)
            CHECK(dt == doctest::Approx(0.1f));

        REQUIRE(log.fixed_since.size() == 5);
        CHECK(log.fixed_since[0] == 0);
        CHECK(log.fixed_since[1] == tick_after_setup);
        CHECK(log.fixed_since[2] == tick_after_setup + 1);
        CHECK(log.fixed_since[3] == tick_after_setup + 2);
        CHECK(log.fixed_since[4] == tick_after_setup + 3);

        CHECK(world.tick_get() == tick_after_setup + 5);
    }

    SUBCASE("a graph that is started again runs every task one time")
    {
        zecs::World world("task-again");
        zecs::TaskGraph graph(0.1f);
        TaskGraphShutdownGuard shutdown_guard(graph, world);

        TaskLog log;
        ScopedTaskLog scoped_log(log);

        graph.init_ids = { "test_task_graph/init_first" };
        graph.step_normal_ids = { "test_task_graph/step_normal" };
        graph.step_fixed_ids = { "test_task_graph/step_fixed" };
        graph.shutdown_ids = { "test_task_graph/shutdown_first" };

        graph.init(world);
        graph.step(world, 0.07f); // too short for a fixed step: 0.07 stays in the graph
        graph.shutdown(world);

        log.order.clear();
        graph.init(world);
        graph.step(world, 0.07f);
        graph.shutdown(world);

        // The time left from the first start isn't added to the second one,
        // so there is no fixed step here too.
        const std::vector<std::string> expected = { "init/first", "step/normal", "shutdown/first" };
        CHECK(log.order == expected);
    }

    SUBCASE("the default time of a fixed step")
    {
        zecs::World world("task-default");
        zecs::TaskGraph graph;
        TaskGraphShutdownGuard shutdown_guard(graph, world);

        TaskLog log;
        ScopedTaskLog scoped_log(log);

        graph.step_fixed_ids = { "test_task_graph/step_fixed" };
        graph.init(world);
        graph.step(world, 0.1f);

        // 0.1 second has three steps of 0.027 in it.
        REQUIRE(log.fixed_dt.size() == 3);
        CHECK(log.fixed_dt[0] == doctest::Approx(zecs::TaskGraph::FIXED_TIME_DEFAULT));
        CHECK(zecs::TaskGraph::FIXED_TIME_DEFAULT == doctest::Approx(0.027f));
    }

    SUBCASE("skip keeps previous since until task completes without skipping")
    {
        zecs::World world("task-skip");
        const zecs::Entity entity = world.entity_add();
        REQUIRE(world.comp_add<test::Position>(entity, 7, 9) != nullptr);

        zecs::TaskGraph graph;
        TaskGraphShutdownGuard shutdown_guard(graph, world);

        TaskLog log;
        log.normal_skip_budget = 1;
        log.normal_patch_target = entity;
        ScopedTaskLog scoped_log(log);

        graph.step_normal_ids = { "test_task_graph/step_normal" };
        graph.init(world);

        const zecs::GTick tick_after_setup = world.tick_get();

        graph.step(world, 0.2f);
        graph.step(world, 0.2f);
        graph.step(world, 0.2f);

        REQUIRE(log.normal_since.size() == 3);
        CHECK(log.normal_since[0] == 0);
        CHECK(log.normal_since[1] == 0);
        CHECK(log.normal_since[2] == tick_after_setup);

        CHECK(world.tick_get() == tick_after_setup + 2);
    }
}
