#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

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
