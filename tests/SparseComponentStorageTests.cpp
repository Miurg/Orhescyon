#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/Components/SparseComponentStorage.hpp>

using namespace Orhescyon;

namespace
{
struct SparsePosition
{
    float x, y;
};

struct TrackedBlob
{
    static inline int aliveCount = 0;
    int value = 0;

    explicit TrackedBlob(int v) : value(v) { ++aliveCount; }
    TrackedBlob(TrackedBlob&& other) noexcept : value(other.value) { ++aliveCount; }
    TrackedBlob& operator=(TrackedBlob&& other) noexcept
    {
        value = other.value;
        return *this;
    }
    ~TrackedBlob() { --aliveCount; }
};
} // namespace

TEST(SparseComponentStorage, AddAndGet)
{
    SparseComponentStorage<SparsePosition> storage;
    storage.addComponent(Entity{1, 0}, SparsePosition{1.0f, 2.0f});

    SparsePosition* p = storage.getComponent(Entity{1, 0});
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 1.0f);
    EXPECT_FLOAT_EQ(p->y, 2.0f);
}

TEST(SparseComponentStorage, GetNonexistentReturnsNull)
{
    SparseComponentStorage<SparsePosition> storage;
    EXPECT_EQ(storage.getComponent(Entity{999, 0}), nullptr);

    for (uint32_t i = 0; i < 1000; i++)
    {
        storage.addComponent(Entity{i, 0}, SparsePosition{1.0f, 2.0f});
    }
    EXPECT_EQ(storage.getComponent(Entity{1001, 0}), nullptr);
}

TEST(SparseComponentStorage, HasComponent)
{
    SparseComponentStorage<SparsePosition> storage;
    storage.addComponent(Entity{7, 0}, SparsePosition{1.0f, 2.0f});

    EXPECT_TRUE(storage.hasComponent(Entity{7, 0}));
    EXPECT_FALSE(storage.hasComponent(Entity{8, 0}));
    EXPECT_FALSE(storage.hasComponent(Entity{100'000, 0}));
}

TEST(SparseComponentStorage, OverwriteExistingKeepsAddress)
{
    SparseComponentStorage<SparsePosition> storage;
    SparsePosition* first = storage.addComponent(Entity{1, 0}, SparsePosition{1.0f, 2.0f});
    SparsePosition* second = storage.addComponent(Entity{1, 0}, SparsePosition{9.0f, 9.0f});

    EXPECT_EQ(first, second);
    EXPECT_FLOAT_EQ(first->x, 9.0f);
    EXPECT_EQ(storage.size(), 1u);
}

TEST(SparseComponentStorage, RemoveComponent)
{
    SparseComponentStorage<SparsePosition> storage;
    storage.addComponent(Entity{1, 0}, SparsePosition{1.0f, 2.0f});
    storage.removeComponent(Entity{1, 0});

    EXPECT_EQ(storage.getComponent(Entity{1, 0}), nullptr);
    EXPECT_FALSE(storage.hasComponent(Entity{1, 0}));
    EXPECT_EQ(storage.size(), 0u);
}

TEST(SparseComponentStorage, RemoveNonexistentIsNoop)
{
    SparseComponentStorage<SparsePosition> storage;
    EXPECT_NO_THROW(storage.removeComponent(Entity{999, 0}));
}

TEST(SparseComponentStorage, DestructorCalledOnRemove)
{
    TrackedBlob::aliveCount = 0;
    SparseComponentStorage<TrackedBlob> storage;
    storage.addComponent(Entity{1, 0}, TrackedBlob{7});
    EXPECT_EQ(TrackedBlob::aliveCount, 1);

    storage.removeComponent(Entity{1, 0});
    EXPECT_EQ(TrackedBlob::aliveCount, 0);
}

TEST(SparseComponentStorage, DestructorsRunOnStorageDestruction)
{
    TrackedBlob::aliveCount = 0;
    {
        SparseComponentStorage<TrackedBlob> storage;
        storage.addComponent(Entity{1, 0}, TrackedBlob{1});
        storage.addComponent(Entity{2, 0}, TrackedBlob{2});
        EXPECT_EQ(TrackedBlob::aliveCount, 2);
    }
    EXPECT_EQ(TrackedBlob::aliveCount, 0);
}

TEST(SparseComponentStorage, OverwriteDoesNotLeak)
{
    TrackedBlob::aliveCount = 0;
    {
        SparseComponentStorage<TrackedBlob> storage;
        storage.addComponent(Entity{1, 0}, TrackedBlob{1});
        storage.addComponent(Entity{1, 0}, TrackedBlob{2});
        EXPECT_EQ(TrackedBlob::aliveCount, 1);
        EXPECT_EQ(storage.getComponent(Entity{1, 0})->value, 2);
    }
    EXPECT_EQ(TrackedBlob::aliveCount, 0);
}

TEST(SparseComponentStorage, PoolSlotReusedAfterRemove)
{
    TrackedBlob::aliveCount = 0;
    SparseComponentStorage<TrackedBlob> storage;

    TrackedBlob* first = storage.addComponent(Entity{1, 0}, TrackedBlob{1});
    storage.removeComponent(Entity{1, 0});

    // The freed pool slot is recycled for the next entity via placement-new
    TrackedBlob* second = storage.addComponent(Entity{2, 0}, TrackedBlob{2});

    EXPECT_EQ(first, second);
    EXPECT_EQ(second->value, 2);
    EXPECT_EQ(TrackedBlob::aliveCount, 1);
}

TEST(SparseComponentStorage, StatisticsIncludeIndexOverhead)
{
    SparseComponentStorage<SparsePosition> storage;
    storage.addComponent(Entity{100, 0}, SparsePosition{1.0f, 2.0f});

    StorageStatistics stats = storage.statistics();
    EXPECT_EQ(stats.liveComponentCount, 1u);
    EXPECT_EQ(stats.allocatedBlockCount, 1u);
    EXPECT_EQ(stats.liveComponentBytes, sizeof(SparsePosition));
    EXPECT_GT(stats.indexOverheadBytes, 0u);
}

TEST(SparseComponentStorage, PresenceWordsExposeBits)
{
    SparseComponentStorage<SparsePosition> storage;
    storage.addComponent(Entity{0, 0}, SparsePosition{1.0f, 2.0f});
    storage.addComponent(Entity{65, 0}, SparsePosition{1.0f, 2.0f});

    EXPECT_EQ(storage.presenceWord(0), uint64_t{1});
    EXPECT_EQ(storage.presenceWord(1), uint64_t{1} << 1);
    EXPECT_EQ(storage.presenceWord(100), 0u);
}
