// Compile-check: parallelFor must accept a move-only callable.

#include <Orhescyon/Jobs/JobPool.hpp>

#include <memory>
#include <utility>

namespace
{
    struct MoveOnlyCallable
    {
        std::unique_ptr<int> owned = std::make_unique<int>(0);
        MoveOnlyCallable() = default;
        MoveOnlyCallable(MoveOnlyCallable&&) = default;
        MoveOnlyCallable(const MoveOnlyCallable&) = delete;
        MoveOnlyCallable& operator=(MoveOnlyCallable&&) = default;
        MoveOnlyCallable& operator=(const MoveOnlyCallable&) = delete;
        void operator()(int) const {}
    };
}

int main()
{
    Orhescyon::JobPool pool(2);
    pool.parallelFor(0, 1, MoveOnlyCallable{});
    return 0;
}
