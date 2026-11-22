#pragma once

#include "zecs/exports.h"
#include "zecs/types.h"

#include <chrono>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

namespace zecs
{

struct World;
using Task = void (*)(World&, TickContext&);

struct ZECS_EXPORT TaskManager
{
    void add(const char* task_id, Task task);
    Task get(const char* task_id);

    static TaskManager* instance();

private:
    TaskManager() = default;
    std::unordered_map<std::string, Task> tasks;
};

struct ZECS_EXPORT TaskGraph
{
    // Seconds between two runs of the fixed tasks: about 37 runs in a second.
    static constexpr float FIXED_TIME_DEFAULT = 0.027f;

    TaskGraph(float _fixed_dt = FIXED_TIME_DEFAULT) : fixed_dt(_fixed_dt) { }
    ~TaskGraph();

    // Finds the tasks by the IDs in the lists below and runs the init tasks.
    // The graph can be used again after shutdown(): init() reads the lists
    // again, and every task starts from the beginning.
    void init(World& world);
    void step(World& world); // dt will be automatically calculated and limited by step_dt_limit
    void step(World& world, float dt);
    void shutdown(World& world);

    inline bool is_initialized() const { return initialized; }

    std::vector<std::string> init_ids;
    std::vector<std::string> step_normal_ids;
    std::vector<std::string> step_fixed_ids;
    std::vector<std::string> shutdown_ids;

    float step_dt_limit = 0.25f;

protected:
    // steady_clock never goes back, also when the time of the computer
    // is changed. So dt is never less than zero.
    using TaskClock = std::chrono::steady_clock;
    using TaskTimePoint = std::chrono::time_point<TaskClock>;
    using TaskList = std::vector<std::pair<Task, TickContext>>;

    bool initialized = false;
    float fixed_dt = FIXED_TIME_DEFAULT;
    float fixed_accumulator = 0.0f;
    TaskTimePoint prev_time_point;

    TaskList tasks_init;
    TaskList tasks_step_normal;
    TaskList tasks_step_fixed;
    TaskList tasks_shutdown;

    // Fills `tasks` with the tasks of `ids`. The old tasks of the list are removed.
    static void tasks_find(const std::vector<std::string>& ids, TaskList& tasks);

    void exec_with_context(World& world, TaskList& tasks, float dt);
    void exec_without_context(World& world, TaskList& tasks);
};

} // namespace zecs
