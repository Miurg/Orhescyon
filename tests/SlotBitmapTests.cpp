#include <gtest/gtest.h>
#include <Orhescyon/Entitys/SlotBitmap.hpp>

#include <vector>

using namespace Orhescyon;

TEST(SlotBitmap, SetAndTest)
{
    SlotBitmap bitmap;
    bitmap.set(0);
    bitmap.set(63);
    bitmap.set(64);
    bitmap.set(4096);

    EXPECT_TRUE(bitmap.test(0));
    EXPECT_TRUE(bitmap.test(63));
    EXPECT_TRUE(bitmap.test(64));
    EXPECT_TRUE(bitmap.test(4096));

    EXPECT_FALSE(bitmap.test(1));
    EXPECT_FALSE(bitmap.test(62));
    EXPECT_FALSE(bitmap.test(65));
    EXPECT_FALSE(bitmap.test(4095));
}

TEST(SlotBitmap, TestBeyondCapacityIsFalse)
{
    SlotBitmap bitmap;
    bitmap.set(10);

    EXPECT_FALSE(bitmap.test(1'000'000));
}

TEST(SlotBitmap, SetDuplicateKeepsCount)
{
    SlotBitmap bitmap;
    bitmap.set(5);
    bitmap.set(5);

    EXPECT_EQ(bitmap.setBitCount(), 1u);
    EXPECT_TRUE(bitmap.test(5));
}

TEST(SlotBitmap, ClearUpdatesCount)
{
    SlotBitmap bitmap;
    bitmap.set(1);
    bitmap.set(2);
    bitmap.set(3);
    bitmap.clear(2);

    EXPECT_EQ(bitmap.setBitCount(), 2u);
    EXPECT_TRUE(bitmap.test(1));
    EXPECT_FALSE(bitmap.test(2));
    EXPECT_TRUE(bitmap.test(3));
}

TEST(SlotBitmap, ClearUnsetSlotIsNoop)
{
    SlotBitmap bitmap;
    bitmap.set(1);
    bitmap.clear(2);

    EXPECT_EQ(bitmap.setBitCount(), 1u);
}

TEST(SlotBitmap, ClearBeyondCapacityIsNoop)
{
    SlotBitmap bitmap;
    bitmap.set(1);
    bitmap.clear(1'000'000);

    EXPECT_EQ(bitmap.setBitCount(), 1u);
    EXPECT_TRUE(bitmap.test(1));
}

TEST(SlotBitmap, WordReflectsSetBits)
{
    SlotBitmap bitmap;
    bitmap.set(0);
    bitmap.set(1);
    bitmap.set(63);

    const uint64_t expected = uint64_t{1} | uint64_t{1} << 1 | uint64_t{1} << 63;
    EXPECT_EQ(bitmap.word(0), expected);
}

TEST(SlotBitmap, WordBoundaryFallsIntoNextWord)
{
    SlotBitmap bitmap;
    bitmap.set(63);
    bitmap.set(64);

    EXPECT_EQ(bitmap.word(0), uint64_t{1} << 63);
    EXPECT_EQ(bitmap.word(1), uint64_t{1});
}

TEST(SlotBitmap, WordBeyondCapacityIsZero)
{
    SlotBitmap bitmap;
    bitmap.set(0);

    EXPECT_EQ(bitmap.word(100), 0u);
}

TEST(SlotBitmap, ReserveSlotsGrowsWordCount)
{
    SlotBitmap bitmap;
    bitmap.reserveSlots(129);

    EXPECT_EQ(bitmap.wordCount(), 3u);
    EXPECT_EQ(bitmap.setBitCount(), 0u);

    bitmap.reserveSlots(64);
    EXPECT_EQ(bitmap.wordCount(), 3u);
}

TEST(SlotBitmap, GrowthKeepsExistingBits)
{
    SlotBitmap bitmap;
    bitmap.set(3);
    bitmap.set(100'000);

    EXPECT_TRUE(bitmap.test(3));
    EXPECT_TRUE(bitmap.test(100'000));
    EXPECT_EQ(bitmap.setBitCount(), 2u);
}

TEST(SlotBitmap, ClearAllKeepsCapacity)
{
    SlotBitmap bitmap;
    bitmap.set(10);
    bitmap.set(500);
    const uint32_t wordsBefore = bitmap.wordCount();

    bitmap.clearAll();

    EXPECT_EQ(bitmap.setBitCount(), 0u);
    EXPECT_FALSE(bitmap.test(10));
    EXPECT_FALSE(bitmap.test(500));
    EXPECT_EQ(bitmap.wordCount(), wordsBefore);
}

TEST(SlotBitmap, ForEachSetBitVisitsAllInOrder)
{
    SlotBitmap bitmap;
    const std::vector<uint32_t> slots = {0, 1, 63, 64, 200, 4095, 4096};
    for (uint32_t slot : slots)
    {
        bitmap.set(slot);
    }

    std::vector<uint32_t> visited;
    bitmap.forEachSetBit([&](uint32_t slot) { visited.push_back(slot); });

    EXPECT_EQ(visited, slots);
}

TEST(SlotBitmap, ForEachSetBitOnEmptyBitmapDoesNothing)
{
    SlotBitmap bitmap;

    uint32_t calls = 0;
    bitmap.forEachSetBit([&](uint32_t) { ++calls; });

    EXPECT_EQ(calls, 0u);
}

TEST(SlotBitmap, DenseRangeRoundTrip)
{
    SlotBitmap bitmap;
    for (uint32_t slot = 0; slot < 1000; ++slot)
    {
        bitmap.set(slot);
    }

    EXPECT_EQ(bitmap.setBitCount(), 1000u);

    uint32_t expectedSlot = 0;
    bitmap.forEachSetBit(
        [&](uint32_t slot)
        {
            EXPECT_EQ(slot, expectedSlot);
            ++expectedSlot;
        });
    EXPECT_EQ(expectedSlot, 1000u);
}
