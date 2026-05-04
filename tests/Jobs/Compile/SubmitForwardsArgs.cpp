// Compile-check: fire-and-forget submit must accept extra args alongside the callable.

#include <Orhescyon/Jobs/JobPool.hpp>

int main()
{
    Orhescyon::JobPool pool(2);
    pool.submit([](int, int, int) {}, 1, 2, 3);
    return 0;
}
