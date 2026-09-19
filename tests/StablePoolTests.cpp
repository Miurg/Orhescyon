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

namespace
{
struct PoolDestructorProbe
{
    int* destructorCalls;
    ~PoolDestructorProbe() { ++*destructorCalls; }
};
} // namespace

TEST(StablePool, GrowingAndDestroyingThePoolDoesNotDestroyStoredObjects)
{
    // Only argument temporaries should be destroyed here; the pool releases raw storage.
    int destructorCalls = 0;
    {
        StablePool<PoolDestructorProbe, 4> pool;
        pool.reserveBlocks(32);
        EXPECT_EQ(destructorCalls, 0);
        EXPECT_EQ(pool.capacity(), 0u);
        EXPECT_EQ(pool.blockCount(), 0u);
        for (int i = 0; i < 17; ++i)
        {
            pool.allocate(PoolDestructorProbe{&destructorCalls});
            EXPECT_EQ(destructorCalls, i + 1);
        }
        EXPECT_EQ(pool.liveCount(), 17u);
        EXPECT_EQ(pool.blockCount(), 5u);
    }
    EXPECT_EQ(destructorCalls, 17);
}

TEST(StablePool, OwnerDestructionIsNotRepeatedByDeallocationOrPoolDestruction)
{
    int destructorCalls = 0;
    {
        StablePool<PoolDestructorProbe, 4> pool;
        auto [index, pointer] = pool.allocate(PoolDestructorProbe{&destructorCalls});
        EXPECT_EQ(destructorCalls, 1);
        pointer->~PoolDestructorProbe();
        EXPECT_EQ(destructorCalls, 2);
        pool.deallocate(index);
        EXPECT_EQ(destructorCalls, 2);
        EXPECT_EQ(pool.liveCount(), 0u);
        EXPECT_EQ(pool.freeCount(), 1u);
    }
    EXPECT_EQ(destructorCalls, 2);
}

TEST(StablePool, ExcessiveReservationThrowsAndLeavesThePoolUsable)
{
    StablePool<int, 64> pool;
    constexpr uint32_t excessiveBlocks = (uint32_t{1} << 26) + 1;
    EXPECT_THROW(pool.reserveBlocks(excessiveBlocks), std::length_error);
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.blockCount(), 0u);
    auto [index, pointer] = pool.allocate(42);
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(index, 0u);
    EXPECT_EQ(*pointer, 42);
    EXPECT_EQ(pool.liveCount(), 1u);
}

#if GTEST_HAS_DEATH_TEST
TEST(StablePoolDeathTest, MutableAccessToEmptyPoolTerminatesWithDiagnostic)
{
    EXPECT_DEATH(
        {
            StablePool<int> pool;
            (void)pool[0];
        },
        "out of bounds");
}

TEST(StablePoolDeathTest, ConstAccessToEmptyPoolTerminatesWithDiagnostic)
{
    EXPECT_DEATH(
        {
            const StablePool<int> pool;
            (void)pool[0];
        },
        "out of bounds");
}
#endif
