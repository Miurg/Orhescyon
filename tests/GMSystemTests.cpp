#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

struct Position { float x, y; };
struct Velocity { float dx, dy; };
struct Health { int value; };

class MovementSystem : public SystemCore<MovementSystem, Position, Velocity>
{
public:
    int updateCallCount = 0;

    void update(GeneralManager& gm) override
    {
        ++updateCallCount;
    }

    void onEntitySubscribed(Entity entity, GeneralManager& gm) override
    {
        SystemCore::onEntitySubscribed(entity, gm);
    }
};

class HealthSystem : public SystemCore<HealthSystem, Health>
{
public:
    bool entitySubscribed = false;
    bool entityUnsubscribed = false;

    void update(GeneralManager& gm) override {}

    void onEntitySubscribed(Entity entity, GeneralManager& gm) override
    {
        entitySubscribed = true;
    }

    void onEntityUnsubscribed(Entity entity, GeneralManager& gm) override
    {
        entityUnsubscribed = true;
    }
};


TEST(GeneralManager, RegisterAndUpdateSystem)
{
    GeneralManager gm;
    gm.registerSystem<MovementSystem>();

    gm.update();
    gm.update();

    SUCCEED();
}

TEST(GeneralManager, SubscribeEntityToSystem)
{
    GeneralManager gm;
    gm.registerSystem<MovementSystem>();

    Entity e = gm.createEntity();
    gm.addComponent<Position>(e, 0.0f, 0.0f);
    gm.addComponent<Velocity>(e, 1.0f, 1.0f);

    EXPECT_NO_THROW(gm.subscribeEntity<MovementSystem>(e));
}

TEST(GeneralManager, SubscribeWithoutRequiredComponentFails)
{
    GeneralManager gm;
    gm.registerSystem<MovementSystem>();

    Entity e = gm.createEntity();

    EXPECT_NO_THROW(gm.subscribeEntity<MovementSystem>(e));
}

TEST(GeneralManager, UnsubscribeEntity)
{
    GeneralManager gm;
    gm.registerSystem<HealthSystem>();

    Entity e = gm.createEntity();
    gm.addComponent<Health>(e, 50);
    gm.subscribeEntity<HealthSystem>(e);

    EXPECT_NO_THROW(gm.unsubscribeEntity<HealthSystem>(e));
}

TEST(GeneralManager, DestroyEntityUnsubscribesFromSystems)
{
    GeneralManager gm;
    gm.registerSystem<HealthSystem>();

    Entity e = gm.createEntity();
    gm.addComponent<Health>(e, 50);
    gm.subscribeEntity<HealthSystem>(e);

    EXPECT_NO_THROW(gm.destroyEntity(e));
}

TEST(GeneralManager, RemoveRequiredComponentAutoUnsubscribes)
{
    GeneralManager gm;
    gm.registerSystem<MovementSystem>();

    Entity e = gm.createEntity();
    gm.addComponent<Position>(e, 0.0f, 0.0f);
    gm.addComponent<Velocity>(e, 1.0f, 0.0f);
    gm.subscribeEntity<MovementSystem>(e);

    EXPECT_NO_THROW(gm.removeComponent<Position>(e));
}
