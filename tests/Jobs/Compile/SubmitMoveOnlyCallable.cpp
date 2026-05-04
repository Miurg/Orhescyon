// Compile-check: fire-and-forget submit must accept a move-only callable.

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
        void operator()() const {}
    };
}

int main()
{
    Orhescyon::JobPool pool(2);
    pool.submit(MoveOnlyCallable{});
    return 0;
}
