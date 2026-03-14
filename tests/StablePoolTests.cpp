#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

TEST(StablePool, AllocateAndAccess)
{
    StablePool<int> pool;
    auto [idx, ptr] = pool.allocate(42);

    EXPECT_EQ(*ptr, 42);
    EXPECT_EQ(pool[idx], 42);
    EXPECT_EQ(pool.liveCount(), 1u);
}

TEST(StablePool, DeallocateDecrementsLiveCount)
{
    StablePool<int> pool;
    auto [idx, ptr] = pool.allocate(10);
    pool.deallocate(idx);

    EXPECT_EQ(pool.liveCount(), 0u);
    EXPECT_EQ(pool.freeCount(), 1u);
}

TEST(StablePool, ReusesFreeSlot)
{
    StablePool<int> pool;
    auto [idx1, ptr1] = pool.allocate(1);
    pool.deallocate(idx1);

    auto [idx2, ptr2] = pool.allocate(2);

	// New allocation should reuse the freed slot, so indices should match
    EXPECT_EQ(idx1, idx2);
    EXPECT_EQ(*ptr2, 2);
}

TEST(StablePool, PointerStability)
{
    StablePool<int, 4> pool; 

    std::vector<int*> ptrs;
    for (int i = 0; i < 20; ++i)
    {
        auto [idx, ptr] = pool.allocate(int(i));
        ptrs.push_back(ptr);
    }

	// New allocations should not invalidate existing pointers
    for (int i = 0; i < 20; ++i)
    {
        EXPECT_EQ(*ptrs[i], i);
    }
}
