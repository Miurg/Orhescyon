#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/Entitys/EntityManager.hpp>

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
