# ZECS

[![CI](https://github.com/smile4u/zecs/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/smile4u/zecs/actions/workflows/ci.yml)
[![License: Apache 2.0](.github/badges/license.svg)](LICENSE)
[![C++20](.github/badges/cpp.svg)](https://en.cppreference.com/w/cpp/20)

Small and powerful C++20 entity component system (ECS) with built-in component change tracking.

## About the project

ZECS is a pet project. It comes from more than 25 years of my work in the
game industry with different engines, and some of them were custom ones.

The whole design was made by a human, and most of the core was written by
a human too: by me. AI was used, and will be used, for tests, for code
review, to look for bugs, and to write some parts of the code under my
control. It can also be used to write the documentation and the comments
in the code.

The main idea of ZECS: it tracks changed components out of the box.
Code that reacts to changes visits only the components that were changed,
and doesn't look at all the others. See [Change tracking](#change-tracking).

Other things that it has:

- Components of one type live together in a pool, in pages of memory.
- A loop over a view visits only the entities that have the components.
- Signals for added, removed and patched components. They are here, but
  I believe that in most cases you don't need them: use the tracking of
  patched components.
- Tasks with a fixed time step.
- Nothing else to install: the only third-party code is in the repository.
  About 3000 lines of code.

## How to build

ZECS needs a C++20 compiler and CMake 3.21 or newer. It is tested with
gcc 14, clang 18, Apple clang and MSVC 2022.

```cmake
add_subdirectory(zecs)
target_link_libraries(my_game PRIVATE zecs)
```

## Example

```cpp
#include <zecs/world.h>

struct Position { float x = 0, y = 0; };
struct Velocity { float dx = 0, dy = 0; };

int main()
{
    zecs::World world;

    const zecs::Entity e = world.entity_add();
    world.comp_add<Position>(e, 1.0f, 2.0f);
    world.comp_add<Velocity>(e, 0.5f, 0.0f);

    for (zecs::Entity e : world.view_comp_group<Position, Velocity>())
        world.comp_get<Position>(e)->x += world.comp_get<Velocity>(e)->dx;
}
```

## Change tracking

A game has many systems that need to know what was changed: the renderer
updates the matrices of the objects that moved, the network sends only new
values, the editor shows what the user changed. The usual ways are a "dirty"
flag in every component or a list of changes that somebody must fill and
clear. ZECS has this inside the pool.

### Ticks

A world has a counter, the tick. It is a 64-bit number that only grows.

- When a component is added or patched, it gets the current tick, and the
  tick of the world grows by one. So every change has its own number,
  and a bigger number means a later change.
- A pool keeps a list of its components in the order of their ticks,
  the newest first. To add or patch a component moves it to the head
  of the list. It costs the same for a pool of any size.
- `view_patched_comps` goes over this list from the head and stops at the
  first component that is older than the tick you gave. So the loop costs
  as much as the number of changed components, not the size of the pool.

ZECS doesn't see a write to a component. Call `comp_patch` after you
change one.

```cpp
zecs::TickContext ctx(world.tick_get());   // remember "now"

world.comp_get<Position>(e)->x = 5;
world.comp_patch<Position>(e);

for (zecs::Entity e : world.view_patched_comps<Position>(ctx))
    ...   // only the components added or patched after "now"
```

A tick is not a frame and not a time. Nothing is cleared at the end of
a frame: every reader keeps its own tick and gets the changes made after it.
So two systems that run at different rates both get every changed component,
and get it one time.

The body of a loop over `view_patched_comps` can add, remove or patch any
component.

### Ticks in tasks

`TaskGraph` keeps a tick for every task: the moment of its last run.
It comes to the task in `ctx`, so a task sees the changes made after its
last run with no code for it.

```cpp
void task_render(zecs::World& world, zecs::TickContext& ctx)
{
    for (zecs::Entity e : world.view_patched_comps<Position>(ctx))
        ...   // the positions changed after the last run of this task
}
```

A task that can't do its work now calls `ctx.skip()`. Then its tick stays
the same, and the next run gets these changes again.

## Rules

ZECS checks these rules with `assert` only where the text says so.
A release build doesn't check them.

**Pointers to components.** A pointer to a component is valid until a
component of the same type is removed: the pool moves its last component into
the free place. Set `stable_pointers = true` in a type to keep its components
in place. Then a pointer is valid until this component is removed or
`World::compact()` is called.

**Removing in a loop.** In a loop over a view, the body can remove components
of the current entity and of the entities that the loop visited already.
If the body removes a component from an entity that the loop didn't visit
yet, the loop can visit some entity two times. To remove such components, put
the entities in a list and remove them after the loop. This rule isn't for
types with `stable_pointers = true` and for `view_patched_comps`.

**Signal handlers.** A handler must not add or remove components of the type
of its signal, and must not connect or disconnect handlers of this type.
It can change components of other types. Checked by `assert`.

**Component types.** A component type is written without `const`:
`comp_get<Position>`, not `comp_get<const Position>`. The second one doesn't
compile.

**Threads.** A world has no locks. Use one world from one thread at a time.

## Benchmark

[zecs_bench](https://github.com/smile4u/zecs_bench) compares ZECS with
[EnTT](https://github.com/skypjack/entt). It has the code of every test,
the results and the steps to run it on your machine.

![zecs vs entt on macOS](https://raw.githubusercontent.com/smile4u/zecs_bench/main/results/charts/macos.svg)

The bars show how much more (+) or less (-) time zecs takes than entt.
Left of 0 zecs is faster. The results are from macOS on Apple M1 Max.

## Limits

- No more than 1 048 575 entities can live in a world at the same time.
  An entity index can be used 4095 times, so a world can make about
  4 billion entities in its life. After that `entity_add` returns
  `Entity_INVALID`.
- The type ID of a component (`zecs::GUid`) comes from the name of the type
  as the compiler prints it. Don't save it to a file and don't send it to
  another program. If two types get the same ID, the program stops at the
  start and prints their names.
- The events API (`event_push`, `view_events`) is a draft. It is off by
  default.

## License

ZECS is under the [Apache License 2.0](LICENSE).

It includes [Signals](https://github.com/pbhogan/Signals) by Patrick Hogan
(MIT) with FastDelegate by Don Clugston (public domain), see
[`ext/signal/LICENSE.txt`](ext/signal/LICENSE.txt). The tests use
[doctest](https://github.com/doctest/doctest) (MIT).

## Thanks

Thanks to [EnTT](https://github.com/skypjack/entt) and its author Michele Caini ([@skypjack](https://github.com/skypjack)) for the inspiration.

Thanks to [@salikq](https://github.com/salikq), my personal static analyzer for ideas.
