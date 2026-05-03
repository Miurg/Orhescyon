// Compile-check: parallelFor range overload must accept random-access iterators.

#include <Orhescyon/Jobs/JobPool.hpp>

#include <vector>

int main()
{
    Orhescyon::JobPool pool(2);
    std::vector<int> items{1, 2, 3, 4};
    pool.parallelFor(items, [](int&) {});
    return 0;
}
