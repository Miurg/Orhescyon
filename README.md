# Orhescyon

A lightweight, header-only ECS library for C++20 with explicit system subscriptions and zero-overhead component access.

Part of the [Halcyon](https://github.com/Miurg/Halcyon) engine.

---

## Features

- **Column component storage** - components indexed directly by the entity slot, pointers stay valid for the entity's lifetime
- **Opt-in sparse storage** - one marker line for big components that live on few entities
- **Explicit system subscriptions** - you control what processes what, no hidden magic
- **Bitmap joins** - iteration ANDs subscription and presence bitmaps 64 entities at a time; dense runs are contiguous loops the compiler can vectorize
- **Advanced parallelism** – systems can be run in parallel
- **Build-time check tiers** - release builds pay for zero validation

---

## Requirements

- C++20
- CMake 3.21+

---

## Integration

### FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    Orhescyon
    GIT_REPOSITORY https://github.com/Miurg/Orhescyon.git
    GIT_TAG        v0.0.1
)

FetchContent_MakeAvailable(Orhescyon)

target_link_libraries(your_target PRIVATE Orhescyon)
```

## Quick start

```cpp
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

struct Position
{
    float x = 0;
    float y = 0;
};

struct Velocity
{
    float dx = 0;
    float dy = 0;
};

class MovementSystem : public SystemCore<MovementSystem, Position, Velocity>
{
public:
    void update(GeneralManager& gm) override
    {
        forEachSubscribedEntity(gm,
                                [](Entity, Position& position, const Velocity& velocity)
                                {
                                    position.x += velocity.dx;
                                    position.y += velocity.dy;
                                });
    }
};

int main()
{
    GeneralManager gm;
    gm.registerSystem<MovementSystem>();

    Entity entity = gm.createEntity();
    gm.addComponent<Position>(entity, 0.0f, 0.0f);
    gm.addComponent<Velocity>(entity, 1.0f, 0.5f);
    gm.subscribeEntity<MovementSystem>(entity);

    gm.update(); // runs MovementSystem over its subscribers
}
```

## Iteration

Systems iterate their subscribers through a word-level join: the subscription bitmap is
ANDed with the presence bitmap of every requested component, 64 entities at a time.
Fully matching words over column storage walk contiguous memory — a loop the compiler
can auto-vectorize; everything else falls back to a per-bit scan.

Beyond the required components of a system, the join can narrow further:

```cpp
gm.forEachSubscribedEntityWith<MovementSystem, Position, const NavigationAgent>(
    [](Entity entity, Position& position, const NavigationAgent& agent) { /* ... */ });
```

Structural changes are forbidden inside the iteration lambda: no `createEntity`/`destroyEntity`,
no `addComponent`/`removeComponent` — they mutate the bitmaps and blocks being iterated.
Reading and writing component data is what the lambda is for.

## Storage policies

Every component type gets its storage picked at compile time. The default is `Column`:
data indexed directly by the entity slot, dense iteration, stable addresses. Big components
that live on few entities can opt into `Sparse` (an index over a stable pool) with one
marker in the type:

```cpp
struct NavigationGrid
{
    static constexpr auto orhescyonStoragePolicy = Orhescyon::StoragePolicy::Sparse;
    // ...
};
```

Rule of thumb: `Sparse` pays off only for types that are both big and rare — and it drops
the joins it participates in off the vectorized path. Measure instead of guessing:
`gm.storageStatistics<T>()` reports live counts, allocated blocks and bytes per type.

## Build-time checks

| Define | Behavior |
|---|---|
| *(none)* | zero runtime validation — release trusts its inputs |
| `ORHESCYON_LOW_CHECK` | cheap guards against misuse (stale handles, missing components) |
| `ORHESCYON_HIGH_CHECK` | LOW + diagnostics |

## System Lifecycle

```
onRegistered -> onEntitySubscribed -> update (per frame) -> onEntityUnsubscribed -> onShutdown
```

All lifecycle hooks are optional - override only what you need.

## License

MIT
