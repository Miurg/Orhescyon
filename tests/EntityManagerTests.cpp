#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/Entitys/EntityManager.hpp>

#include <atomic>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace Orhescyon;

TEST(EntityManager, CreatedEntityIsActive)
{
    EntityManager manager;
    Entity entity = manager.createEntity();

    EXPECT_TRUE(manager.isActive(entity));
}

TEST(EntityManager, DestroyDeactivates)
{
    EntityManager manager;
    Entity entity = manager.createEntity();
    manager.destroyEntity(entity);

    EXPECT_FALSE(manager.isActive(entity));
}

TEST(EntityManager, DestroyedSlotIsRecycledWithNewGeneration)
{
    EntityManager manager;
    Entity first = manager.createEntity();
    manager.destroyEntity(first);
    Entity second = manager.createEntity();

    EXPECT_EQ(second.slot, first.slot);
    EXPECT_EQ(second.generation, first.generation + 1);
    EXPECT_NE(second, first);
}

TEST(EntityManager, StaleHandleIsNotActive)
{
    EntityManager manager;
    Entity first = manager.createEntity();
    manager.destroyEntity(first);
    Entity second = manager.createEntity();

    EXPECT_FALSE(manager.isActive(first));
    EXPECT_TRUE(manager.isActive(second));
}

TEST(EntityManager, DestroyStaleHandleDoesNotAffectSlotOwner)
{
    EntityManager manager;
    Entity first = manager.createEntity();
    manager.destroyEntity(first);
    Entity second = manager.createEntity();

    // first is stale now; destroying it again must not touch second
    manager.destroyEntity(first);

    EXPECT_TRUE(manager.isActive(second));
}

TEST(EntityManager, DoubleDestroyIsRejected)
{
    EntityManager manager;
    Entity entity = manager.createEntity();
    manager.destroyEntity(entity);
    manager.destroyEntity(entity);

    // A double destroy must not put the slot on the free list twice
    Entity second = manager.createEntity();
    Entity third = manager.createEntity();

    EXPECT_NE(second.slot, third.slot);
}

TEST(EntityManager, InvalidEntityIsNotActive)
{
    EntityManager manager;
    manager.createEntity();

    EXPECT_FALSE(manager.isActive(Entity::invalid()));
}

TEST(EntityManager, ActiveEntityCountTracksCreateAndDestroy)
{
    EntityManager manager;
    EXPECT_EQ(manager.activeEntityCount(), 0u);

    Entity a = manager.createEntity();
    Entity b = manager.createEntity();
    EXPECT_EQ(manager.activeEntityCount(), 2u);

    manager.destroyEntity(a);
    EXPECT_EQ(manager.activeEntityCount(), 1u);

    manager.destroyEntity(b);
    EXPECT_EQ(manager.activeEntityCount(), 0u);
}

TEST(EntityManager, SlotCapacityCountsUniqueSlotsOnly)
{
    EntityManager manager;
    Entity first = manager.createEntity();
    manager.destroyEntity(first);
    manager.createEntity();

    EXPECT_EQ(manager.slotCapacity(), 1u);

    manager.createEntity();
    EXPECT_EQ(manager.slotCapacity(), 2u);
}

TEST(EntityManager, ForEachActiveEntityVisitsExactlyLiveEntities)
{
    EntityManager manager;
    Entity a = manager.createEntity();
    Entity b = manager.createEntity();
    Entity c = manager.createEntity();
    manager.destroyEntity(b);

    std::vector<Entity> visited;
    manager.forEachActiveEntity([&](Entity entity) { visited.push_back(entity); });

    EXPECT_EQ(visited, (std::vector<Entity>{a, c}));
}

TEST(EntityManager, GenerationSeparatesHandleEpochs)
{
    EntityManager manager;
    std::vector<Entity> epochs;
    for (int i = 0; i < 3; ++i)
    {
        Entity entity = manager.createEntity();
        epochs.push_back(entity);
        manager.destroyEntity(entity);
    }
    Entity live = manager.createEntity();

    for (const Entity& stale : epochs)
    {
        EXPECT_EQ(stale.slot, live.slot);
        EXPECT_FALSE(manager.isActive(stale));
    }
    EXPECT_TRUE(manager.isActive(live));
}

TEST(EntityManager, ConcurrentCreationReturnsUniqueActiveEntities)
{
    constexpr std::size_t threadCount = 8;
    constexpr std::size_t entitiesPerThread = 256;
    constexpr std::size_t expectedCount = threadCount * entitiesPerThread;

    EntityManager manager;
    std::vector<std::vector<Entity>> created(threadCount);
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    std::atomic_uint32_t ready{0};
    std::atomic_bool start{false};
    for (std::size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        workers.emplace_back(
            [&, threadIndex]
            {
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();

                std::vector<Entity>& threadEntities = created[threadIndex];
                threadEntities.reserve(entitiesPerThread);
                for (std::size_t i = 0; i < entitiesPerThread; ++i)
                {
                    threadEntities.push_back(manager.createEntity());
                }
            });
    }

    while (ready.load(std::memory_order_acquire) != threadCount) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (std::thread& worker : workers) worker.join();

    std::unordered_set<Entity> uniqueEntities;
    for (const std::vector<Entity>& threadEntities : created)
    {
        for (Entity entity : threadEntities)
        {
            EXPECT_TRUE(uniqueEntities.insert(entity).second);
            EXPECT_TRUE(manager.isActive(entity));
        }
    }

    EXPECT_EQ(uniqueEntities.size(), expectedCount);
    EXPECT_EQ(manager.activeEntityCount(), expectedCount);
    EXPECT_EQ(manager.slotCapacity(), expectedCount);
}

TEST(EntityManager, ConcurrentCreationReusesEveryFreeSlotOnce)
{
    constexpr std::size_t threadCount = 8;
    constexpr std::size_t entitiesPerThread = 32;
    constexpr std::size_t entityCount = threadCount * entitiesPerThread;

    EntityManager manager;
    std::vector<Entity> firstGeneration;
    firstGeneration.reserve(entityCount);
    for (std::size_t i = 0; i < entityCount; ++i) firstGeneration.push_back(manager.createEntity());
    for (Entity entity : firstGeneration) manager.destroyEntity(entity);

    std::vector<std::vector<Entity>> created(threadCount);
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    std::atomic_uint32_t ready{0};
    std::atomic_bool start{false};
    for (std::size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        workers.emplace_back(
            [&, threadIndex]
            {
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();

                std::vector<Entity>& threadEntities = created[threadIndex];
                threadEntities.reserve(entitiesPerThread);
                for (std::size_t i = 0; i < entitiesPerThread; ++i)
                {
                    threadEntities.push_back(manager.createEntity());
                }
            });
    }
    while (ready.load(std::memory_order_acquire) != threadCount) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (std::thread& worker : workers) worker.join();

    std::unordered_set<uint32_t> uniqueSlots;
    for (const std::vector<Entity>& threadEntities : created)
    {
        for (Entity entity : threadEntities)
        {
            EXPECT_TRUE(uniqueSlots.insert(entity.slot).second);
            EXPECT_EQ(entity.generation, 1u);
            EXPECT_TRUE(manager.isActive(entity));
        }
    }

    EXPECT_EQ(uniqueSlots.size(), entityCount);
    EXPECT_EQ(manager.activeEntityCount(), entityCount);
    EXPECT_EQ(manager.slotCapacity(), entityCount);
}

TEST(EntityManager, ConcurrentDestroyRecyclesSlotOnce)
{
    constexpr std::size_t threadCount = 8;

    EntityManager manager;
    Entity target = manager.createEntity();
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    std::atomic_uint32_t ready{0};
    std::atomic_bool start{false};
    for (std::size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        workers.emplace_back(
            [&]
            {
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                manager.destroyEntity(target);
            });
    }

    while (ready.load(std::memory_order_acquire) != threadCount) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (std::thread& worker : workers) worker.join();

    EXPECT_FALSE(manager.isActive(target));
    EXPECT_EQ(manager.activeEntityCount(), 0u);

    Entity recycled = manager.createEntity();
    Entity next = manager.createEntity();
    EXPECT_EQ(recycled.slot, target.slot);
    EXPECT_EQ(recycled.generation, target.generation + 1);
    EXPECT_NE(next.slot, target.slot);
}

TEST(EntityManager, CreationDuringIterationAppearsOnNextIteration)
{
    EntityManager manager;
    Entity first = manager.createEntity();
    Entity createdDuringIteration = Entity::invalid();

    std::vector<Entity> firstVisit;
    manager.forEachActiveEntity(
        [&](Entity entity)
        {
            firstVisit.push_back(entity);
            createdDuringIteration = manager.createEntity();
        });

    EXPECT_EQ(firstVisit, (std::vector<Entity>{first}));
    EXPECT_TRUE(manager.isActive(createdDuringIteration));

    std::vector<Entity> secondVisit;
    manager.forEachActiveEntity([&](Entity entity) { secondVisit.push_back(entity); });

    EXPECT_EQ(secondVisit, (std::vector<Entity>{first, createdDuringIteration}));
}

TEST(EntityManager, ReadsRemainValidDuringConcurrentCreation)
{
    constexpr std::size_t entityCount = 1024;
    constexpr std::size_t readPassCount = 128;

    EntityManager manager;
    std::atomic_bool readerReady{false};
    std::atomic_bool start{false};
    std::atomic_bool invalidRead{false};

    std::thread reader(
        [&]
        {
            readerReady.store(true, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();

            for (std::size_t pass = 0; pass < readPassCount; ++pass)
            {
                const uint32_t count = manager.activeEntityCount();
                const uint32_t capacity = manager.slotCapacity();
                if (count > capacity) invalidRead.store(true, std::memory_order_relaxed);

                if (capacity > 0) static_cast<void>(manager.generationOfSlot(capacity - 1));

                manager.forEachActiveEntity(
                    [&](Entity entity)
                    {
                        if (!manager.isActive(entity)) invalidRead.store(true, std::memory_order_relaxed);
                    });

                std::this_thread::yield();
            }
        });

    while (!readerReady.load(std::memory_order_acquire)) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (std::size_t i = 0; i < entityCount; ++i) manager.createEntity();
    reader.join();

    EXPECT_FALSE(invalidRead.load(std::memory_order_relaxed));
    EXPECT_EQ(manager.activeEntityCount(), entityCount);
}
