#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/Components/ComponentColumn.hpp>

using namespace Orhescyon;

namespace
{
struct ColumnPosition
{
    float x, y;
};

struct TrackedPayload
{
    static inline int aliveCount = 0;
    int value = 0;

    explicit TrackedPayload(int v) : value(v) { ++aliveCount; }
    TrackedPayload(TrackedPayload&& other) noexcept : value(other.value) { ++aliveCount; }
    TrackedPayload& operator=(TrackedPayload&& other) noexcept
    {
        value = other.value;
        return *this;
    }
    ~TrackedPayload() { --aliveCount; }
};

struct RenderableTag
{
};
} // namespace

TEST(ComponentColumn, AddAndGetAcrossBlockBoundary)
{
    ComponentColumn<ColumnPosition> column;
    column.addComponent(Entity{0, 0}, ColumnPosition{1.0f, 2.0f});
    column.addComponent(Entity{4095, 0}, ColumnPosition{3.0f, 4.0f});
    column.addComponent(Entity{4096, 0}, ColumnPosition{5.0f, 6.0f});

    ASSERT_NE(column.getComponent(Entity{0, 0}), nullptr);
    EXPECT_FLOAT_EQ(column.getComponent(Entity{0, 0})->x, 1.0f);
    EXPECT_FLOAT_EQ(column.getComponent(Entity{4095, 0})->x, 3.0f);
    EXPECT_FLOAT_EQ(column.getComponent(Entity{4096, 0})->x, 5.0f);
}

TEST(ComponentColumn, GetMissingReturnsNull)
{
    ComponentColumn<ColumnPosition> column;
    column.addComponent(Entity{1, 0}, ColumnPosition{1.0f, 2.0f});

    EXPECT_EQ(column.getComponent(Entity{2, 0}), nullptr);
    EXPECT_EQ(column.getComponent(Entity{100'000, 0}), nullptr);
}

TEST(ComponentColumn, HasComponent)
{
    ComponentColumn<ColumnPosition> column;
    column.addComponent(Entity{7, 0}, ColumnPosition{1.0f, 2.0f});

    EXPECT_TRUE(column.hasComponent(Entity{7, 0}));
    EXPECT_FALSE(column.hasComponent(Entity{8, 0}));
    EXPECT_FALSE(column.hasComponent(Entity{100'000, 0}));
}

TEST(ComponentColumn, OverwriteKeepsAddressAndUpdatesValue)
{
    ComponentColumn<ColumnPosition> column;
    ColumnPosition* first = column.addComponent(Entity{1, 0}, ColumnPosition{1.0f, 2.0f});
    ColumnPosition* second = column.addComponent(Entity{1, 0}, ColumnPosition{9.0f, 9.0f});

    EXPECT_EQ(first, second);
    EXPECT_FLOAT_EQ(first->x, 9.0f);
    EXPECT_EQ(column.size(), 1u);
}

TEST(ComponentColumn, RemoveClearsPresenceAndSize)
{
    ComponentColumn<ColumnPosition> column;
    column.addComponent(Entity{1, 0}, ColumnPosition{1.0f, 2.0f});
    column.removeComponent(Entity{1, 0});

    EXPECT_FALSE(column.hasComponent(Entity{1, 0}));
    EXPECT_EQ(column.getComponent(Entity{1, 0}), nullptr);
    EXPECT_EQ(column.size(), 0u);
}

TEST(ComponentColumn, RemoveMissingIsNoop)
{
    ComponentColumn<ColumnPosition> column;
    column.addComponent(Entity{1, 0}, ColumnPosition{1.0f, 2.0f});

    EXPECT_NO_THROW(column.removeComponent(Entity{999, 0}));
    EXPECT_EQ(column.size(), 1u);
}

TEST(ComponentColumn, PointerStabilityAcrossGrowth)
{
    ComponentColumn<ColumnPosition> column;
    ColumnPosition* pointer = column.addComponent(Entity{0, 0}, ColumnPosition{1.0f, 2.0f});

    // Far slot forces new blocks and regrowth of the block vector
    column.addComponent(Entity{100'000, 0}, ColumnPosition{3.0f, 4.0f});

    EXPECT_EQ(column.getComponent(Entity{0, 0}), pointer);
    EXPECT_FLOAT_EQ(pointer->x, 1.0f);
}

TEST(ComponentColumn, ReAddAfterRemoveReturnsSameAddress)
{
    ComponentColumn<ColumnPosition> column;
    ColumnPosition* first = column.addComponent(Entity{5, 0}, ColumnPosition{1.0f, 2.0f});
    column.removeComponent(Entity{5, 0});
    ColumnPosition* second = column.addComponent(Entity{5, 0}, ColumnPosition{3.0f, 4.0f});

    // The slot pins the address for the entity's lifetime
    EXPECT_EQ(first, second);
    EXPECT_FLOAT_EQ(second->x, 3.0f);
}

TEST(ComponentColumn, DestructorCalledOnRemove)
{
    TrackedPayload::aliveCount = 0;
    ComponentColumn<TrackedPayload> column;
    column.addComponent(Entity{1, 0}, TrackedPayload{7});
    EXPECT_EQ(TrackedPayload::aliveCount, 1);

    column.removeComponent(Entity{1, 0});
    EXPECT_EQ(TrackedPayload::aliveCount, 0);
}

TEST(ComponentColumn, DestructorsRunOnColumnDestruction)
{
    TrackedPayload::aliveCount = 0;
    {
        ComponentColumn<TrackedPayload> column;
        column.addComponent(Entity{1, 0}, TrackedPayload{1});
        column.addComponent(Entity{2, 0}, TrackedPayload{2});
        column.addComponent(Entity{5000, 0}, TrackedPayload{3});
        EXPECT_EQ(TrackedPayload::aliveCount, 3);
    }
    EXPECT_EQ(TrackedPayload::aliveCount, 0);
}

TEST(ComponentColumn, OverwriteDoesNotLeak)
{
    TrackedPayload::aliveCount = 0;
    {
        ComponentColumn<TrackedPayload> column;
        column.addComponent(Entity{1, 0}, TrackedPayload{1});
        column.addComponent(Entity{1, 0}, TrackedPayload{2});
        EXPECT_EQ(TrackedPayload::aliveCount, 1);
        EXPECT_EQ(column.getComponent(Entity{1, 0})->value, 2);
    }
    EXPECT_EQ(TrackedPayload::aliveCount, 0);
}

TEST(ComponentColumn, TagComponentsAllocateNoBlocks)
{
    ComponentColumn<RenderableTag> column;
    column.addComponent(Entity{3, 0}, RenderableTag{});
    column.addComponent(Entity{70, 0}, RenderableTag{});

    EXPECT_TRUE(column.hasComponent(Entity{3, 0}));
    EXPECT_TRUE(column.hasComponent(Entity{70, 0}));
    EXPECT_FALSE(column.hasComponent(Entity{4, 0}));
    EXPECT_NE(column.getComponent(Entity{3, 0}), nullptr);
    EXPECT_EQ(column.size(), 2u);

    StorageStatistics stats = column.statistics();
    EXPECT_EQ(stats.liveComponentCount, 2u);
    EXPECT_EQ(stats.allocatedBlockCount, 0u);
    EXPECT_EQ(stats.allocatedComponentBytes, 0u);

    column.removeComponent(Entity{3, 0});
    EXPECT_FALSE(column.hasComponent(Entity{3, 0}));
    EXPECT_EQ(column.size(), 1u);
}

TEST(ComponentColumn, StatisticsCountBlocksAndBytes)
{
    ComponentColumn<ColumnPosition> column;
    column.addComponent(Entity{0, 0}, ColumnPosition{1.0f, 2.0f});
    column.addComponent(Entity{4096, 0}, ColumnPosition{3.0f, 4.0f});

    StorageStatistics stats = column.statistics();
    EXPECT_EQ(stats.liveComponentCount, 2u);
    EXPECT_EQ(stats.allocatedBlockCount, 2u);
    EXPECT_EQ(stats.slotsPerBlock, 4096u);
    EXPECT_EQ(stats.allocatedComponentBytes, 2u * 4096u * sizeof(ColumnPosition));
    EXPECT_EQ(stats.liveComponentBytes, 2u * sizeof(ColumnPosition));
    EXPECT_EQ(stats.indexOverheadBytes, 0u);
}

TEST(ComponentColumn, PresenceWordsExposeBits)
{
    ComponentColumn<ColumnPosition> column;
    column.addComponent(Entity{0, 0}, ColumnPosition{1.0f, 2.0f});
    column.addComponent(Entity{1, 0}, ColumnPosition{1.0f, 2.0f});
    column.addComponent(Entity{64, 0}, ColumnPosition{1.0f, 2.0f});

    EXPECT_EQ(column.presenceWord(0), uint64_t{0b11});
    EXPECT_EQ(column.presenceWord(1), uint64_t{1});
    EXPECT_EQ(column.presenceWord(100), 0u);
    EXPECT_GE(column.presenceWordCount(), 2u);
}
