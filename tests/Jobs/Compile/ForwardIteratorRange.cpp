// Compile-check: parallelFor range overload must accept non-random-access iterators.

#include <Orhescyon/Jobs/JobPool.hpp>

#include <list>

int main()
{
    Orhescyon::JobPool pool(2);
    std::list<int> items{1, 2, 3, 4};
    pool.parallelFor(items, [](int&) {});
    return 0;
}
