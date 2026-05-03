// Compile-check: parallelFor must accept an lvalue callable.

#include <Orhescyon/Jobs/JobPool.hpp>

int main()
{
    Orhescyon::JobPool pool(2);
    auto fn = [](int) {};
    pool.parallelFor(0, 1, fn);
    return 0;
}
