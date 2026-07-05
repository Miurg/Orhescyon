#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>

using namespace Orhescyon;

struct Position { float x, y; };
struct Health { int value; };

TEST(GeneralManager, AddAndGetComponent)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponent<Position>(e, 3.0f, 4.0f);

    Position* p = gm.getComponent<Position>(e);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 3.0f);
    EXPECT_FLOAT_EQ(p->y, 4.0f);
}

TEST(GeneralManager, GetMissingComponentReturnsNull)
{
    GeneralManager gm;
    Entity e = gm.createEntity();

    EXPECT_EQ(gm.getComponent<Position>(e), nullptr);
}

TEST(GeneralManager, AddComponentToInactiveReturnsNull)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.destroyEntity(e);

    EXPECT_EQ(gm.addComponent<Position>(e, 0.0f, 0.0f), nullptr);
}

TEST(GeneralManager, RemoveComponent)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponent<Position>(e, 1.0f, 2.0f);
    gm.removeComponent<Position>(e);

    EXPECT_EQ(gm.getComponent<Position>(e), nullptr);
}

TEST(GeneralManager, DestroyEntityClearsComponents)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponent<Position>(e, 1.0f, 2.0f);
    gm.addComponent<Health>(e, 100);
    gm.destroyEntity(e);

    // If entity inactive - getComponent return nullptr
    EXPECT_EQ(gm.getComponent<Position>(e), nullptr);
    EXPECT_EQ(gm.getComponent<Health>(e), nullptr);
}

TEST(GeneralManager, HasComponentReturnsFalseWhenMissing)
{
    GeneralManager gm;
    Entity e = gm.createEntity();

    EXPECT_FALSE(gm.hasComponent<Position>(e));
}

TEST(GeneralManager, HasComponentReturnsTrueAfterAdd)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponent<Position>(e, 1.0f, 2.0f);

    EXPECT_TRUE(gm.hasComponent<Position>(e));
}

TEST(GeneralManager, HasComponentReturnsFalseAfterRemove)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponent<Position>(e, 1.0f, 2.0f);
    gm.removeComponent<Position>(e);

    EXPECT_FALSE(gm.hasComponent<Position>(e));
}

TEST(GeneralManager, HasComponentOnInactiveEntityReturnsFalse)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponent<Position>(e, 1.0f, 2.0f);
    gm.destroyEntity(e);

    EXPECT_FALSE(gm.hasComponent<Position>(e));
}

TEST(GeneralManager, RecycledSlotHasNoStaleComponents)
{
    GeneralManager gm;
    Entity first = gm.createEntity();
    gm.addComponent<Position>(first, 1.0f, 2.0f);
    gm.addComponent<Health>(first, 100);
    gm.destroyEntity(first);

    Entity second = gm.createEntity();
    ASSERT_EQ(second.slot, first.slot);

    EXPECT_FALSE(gm.hasComponent<Position>(second));
    EXPECT_FALSE(gm.hasComponent<Health>(second));
    EXPECT_EQ(gm.getComponent<Position>(second), nullptr);
}

namespace
{
struct TrackedResource
{
    static inline int aliveCount = 0;
    int value = 0;

    explicit TrackedResource(int v) : value(v) { ++aliveCount; }
    TrackedResource(TrackedResource&& other) noexcept : value(other.value) { ++aliveCount; }
    TrackedResource& operator=(TrackedResource&& other) noexcept
    {
        value = other.value;
        return *this;
    }
    ~TrackedResource() { --aliveCount; }
};
} // namespace

TEST(GeneralManager, DestroyEntityDestroysComponents)
{
    TrackedResource::aliveCount = 0;
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponent<TrackedResource>(e, 5);
    EXPECT_EQ(TrackedResource::aliveCount, 1);

    gm.destroyEntity(e);
    EXPECT_EQ(TrackedResource::aliveCount, 0);
}

TEST(GeneralManager, ManagerDestructionDestroysComponents)
{
    TrackedResource::aliveCount = 0;
    {
        GeneralManager gm;
        Entity e = gm.createEntity();
        gm.addComponent<TrackedResource>(e, 5);
        EXPECT_EQ(TrackedResource::aliveCount, 1);
    }
    EXPECT_EQ(TrackedResource::aliveCount, 0);
}