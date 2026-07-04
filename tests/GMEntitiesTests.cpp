#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <vector>

using namespace Orhescyon;

TEST(GeneralManager, CreateEntityIsActive)
{
    GeneralManager gm;
    Entity e = gm.createEntity();

    EXPECT_TRUE(gm.isActive(e));
}

TEST(GeneralManager, CreateEntitiesAreUnique)
{
    GeneralManager gm;
    Entity a = gm.createEntity();
    Entity b = gm.createEntity();

    EXPECT_NE(a, b);
}

TEST(GeneralManager, DestroyEntityDeactivates)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.destroyEntity(e);

    EXPECT_FALSE(gm.isActive(e));
}

TEST(GeneralManager, DestroyInactiveEntityIsSafe)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.destroyEntity(e);

    EXPECT_NO_THROW(gm.destroyEntity(e));
}

TEST(GeneralManager, DestroyedSlotIsRecycledWithNewGeneration)
{
    GeneralManager gm;
    Entity first = gm.createEntity();
    gm.destroyEntity(first);
    Entity second = gm.createEntity();

    EXPECT_EQ(second.slot, first.slot);
    EXPECT_NE(second, first);
    EXPECT_FALSE(gm.isActive(first));
    EXPECT_TRUE(gm.isActive(second));
}

TEST(GeneralManager, DestroyStaleHandleDoesNotAffectSlotOwner)
{
    GeneralManager gm;
    Entity first = gm.createEntity();
    gm.destroyEntity(first);
    Entity second = gm.createEntity();

    // first is stale; destroying it again must not destroy second
    gm.destroyEntity(first);

    EXPECT_TRUE(gm.isActive(second));
}

TEST(GeneralManager, ForEachActiveEntityVisitsLiveEntities)
{
    GeneralManager gm;
    Entity a = gm.createEntity();
    Entity b = gm.createEntity();
    Entity c = gm.createEntity();
    gm.destroyEntity(b);

    std::vector<Entity> visited;
    gm.forEachActiveEntity([&](Entity entity) { visited.push_back(entity); });

    EXPECT_EQ(visited, (std::vector<Entity>{a, c}));
    EXPECT_EQ(gm.activeEntityCount(), 2u);
}
