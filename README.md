# Orhescyon

A lightweight, header-only ECS library for C++20 with explicit system subscriptions and zero-overhead component access.

Part of the [Halcyon](https://github.com/Miurg/Halcyon) engine.

---

## Features

- **Sparse/dense entity set** - O(1) insert, erase and lookup
- **Stable component pool** - pointers remain valid across allocations
- **Explicit system subscriptions** - you control what processes what, no hidden magic
- **CRTP system base** - required components checked automatically at subscription time
- **Advance parallelism** – systems can be run in parallel
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

## System Lifecycle

```
onRegistered -> onEntitySubscribed -> update (per frame) -> onEntityUnsubscribed -> onShutdown
```

All lifecycle hooks are optional - override only what you need.

## License

MIT
