// Compile-check: fire-and-forget submit must accept move-only arguments alongside the callable.

#include <Orhescyon/Jobs/JobPool.hpp>

#include <memory>
#include <utility>

int main()
{
    Orhescyon::JobPool pool(2);
    auto owned = std::make_unique<int>(42);
    pool.submit([](std::unique_ptr<int>) {}, std::move(owned));
    return 0;
}
