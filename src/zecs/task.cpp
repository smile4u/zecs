#include "zecs/task.h"
#include "zecs/types.h"
#include "zecs/world.h"

#include <algorithm>
#include <cassert>
#include <chrono>

namespace zecs
{
TaskManager* TaskManager::instance()
{
    static TaskManager tm;
    return &tm;
}

void TaskManager::add(const char* task_id, Task task)
{
    [[maybe_unused]] const auto res = tasks.emplace(std::string(task_id), task);
    assert(res.second); // a task with this ID is here already
}

Task TaskManager::get(const char* task_id)
{
    auto it = tasks.find(std::string(task_id));
    return (it != tasks.end()) ? it->second : nullptr;
}

TaskGraph::~TaskGraph()
{
    assert(!initialized);
}

void TaskGraph::tasks_find(const std::vector<std::string>& ids, TaskList& tasks)
{
    // init() can be called again after shutdown(), so the list is cleared first.
    tasks.clear();
    tasks.reserve(ids.size());

    for (const std::string& task_id : ids)
    {
        Task task = TaskManager::instance()->get(task_id.c_str());
        assert(task != nullptr);
        tasks.push_back({ task, TickContext() });
    }
}

void TaskGraph::init(World& world)
{
    assert(!initialized);

    tasks_find(init_ids, tasks_init);
    tasks_find(step_normal_ids, tasks_step_normal);
    tasks_find(step_fixed_ids, tasks_step_fixed);
    tasks_find(shutdown_ids, tasks_shutdown);

    fixed_accumulator = 0.0f;
    prev_time_point = TaskClock::now();
    exec_without_context(world, tasks_init);
    initialized = true;
}

void TaskGraph::step(World& world)
{
    assert(initialized);

    const TaskTimePoint cur_time_point = TaskClock::now();
    const std::chrono::duration<float> step_duration = cur_time_point - prev_time_point;
    const float dt = std::min(step_duration.count(), step_dt_limit);

    step(world, dt);
    prev_time_point = cur_time_point;
}

void TaskGraph::step(World& world, float dt)
{
    assert(initialized);

    prev_time_point = TaskClock::now();
    exec_with_context(world, tasks_step_normal, dt);

    fixed_accumulator += dt;
    while (fixed_accumulator > fixed_dt)
    {
        fixed_accumulator -= fixed_dt;
        exec_with_context(world, tasks_step_fixed, fixed_dt);
    }
}

void TaskGraph::shutdown(World& world)
{
    assert(initialized);
    exec_without_context(world, tasks_shutdown);
    initialized = false;
}

void TaskGraph::exec_with_context(World& world, TaskGraph::TaskList& tasks, float dt)
{
    for(auto& [task, ctx] : tasks)
    {
        GTick prev_since = ctx.since;
        GTick prev_until = world.tick_get();

        ctx.dt = dt;
        (*task)(world, ctx);

        if (ctx.since != GTick_INVALID)
        {
            assert(ctx.since == prev_since);
            ctx.since = prev_until;
        }
        else
        {
            // skipped by task, revert to previous value
            // to give it chance to update later
            ctx.since = prev_since;
        }
    }
}

void TaskGraph::exec_without_context(World& world, TaskGraph::TaskList& tasks)
{
    GTick cur_tick = world.tick_get();

    TickContext ctx;
    ctx.since = cur_tick;

    for (const auto& [task, _] : tasks)
    {
        (*task)(world, ctx);
    }
}

}
