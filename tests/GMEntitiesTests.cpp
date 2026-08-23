#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <vector>

using namespace Orhescyon;

TEST(GeneralManager, CreateEntityIsActive)
{
    GeneralManager gm;
    Entity e = gm.createEntityImmediate();

    EXPECT_TRUE(gm.isActive(e));
}

TEST(GeneralManager, CreateEntitiesAreUnique)
{
    GeneralManager gm;
    Entity a = gm.createEntityImmediate();
    Entity b = gm.createEntityImmediate();

    EXPECT_NE(a, b);
}

TEST(GeneralManager, DestroyEntityDeactivates)
{
    GeneralManager gm;
    Entity e = gm.createEntityImmediate();
    gm.destroyEntityImmediate(e);

    EXPECT_FALSE(gm.isActive(e));
}

TEST(GeneralManager, DestroyInactiveEntityIsSafe)
{
    GeneralManager gm;
    Entity e = gm.createEntityImmediate();
    gm.destroyEntityImmediate(e);

    EXPECT_NO_THROW(gm.destroyEntityImmediate(e));
}

TEST(GeneralManager, DestroyedSlotIsRecycledWithNewGeneration)
{
    GeneralManager gm;
    Entity first = gm.createEntityImmediate();
    gm.destroyEntityImmediate(first);
    Entity second = gm.createEntityImmediate();

    EXPECT_EQ(second.slot, first.slot);
    EXPECT_NE(second, first);
    EXPECT_FALSE(gm.isActive(first));
    EXPECT_TRUE(gm.isActive(second));
}

TEST(GeneralManager, DestroyStaleHandleDoesNotAffectSlotOwner)
{
    GeneralManager gm;
    Entity first = gm.createEntityImmediate();
    gm.destroyEntityImmediate(first);
    Entity second = gm.createEntityImmediate();

    // first is stale; destroying it again must not destroy second
    gm.destroyEntityImmediate(first);

    EXPECT_TRUE(gm.isActive(second));
}

TEST(GeneralManager, ForEachActiveEntityVisitsLiveEntities)
{
    GeneralManager gm;
    Entity a = gm.createEntityImmediate();
    Entity b = gm.createEntityImmediate();
    Entity c = gm.createEntityImmediate();
    gm.destroyEntityImmediate(b);

    std::vector<Entity> visited;
    gm.forEachActiveEntity([&](Entity entity) { visited.push_back(entity); });

    EXPECT_EQ(visited, (std::vector<Entity>{a, c}));
    EXPECT_EQ(gm.activeEntityCount(), 2u);
}
