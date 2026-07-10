#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <vector>

using namespace Orhescyon;

namespace
{
struct ViewPosition
{
    float x = 0;
    float y = 0;
};

struct ViewVelocity
{
    float dx = 0;
    float dy = 0;
};

struct ViewRareBlob
{
    static constexpr auto orhescyonStoragePolicy = StoragePolicy::Sparse;
    int payload = 0;
};

class ViewMovementSystem : public SystemCore<ViewMovementSystem, ViewPosition, ViewVelocity>
{
public:
    void update(GeneralManager&) override {}
};

// Iterates its subscribers from update() through the CRTP helper
class IntegrationSystem : public SystemCore<IntegrationSystem, ViewPosition, ViewVelocity>
{
public:
    void update(GeneralManager& gm) override
    {
        forEachSubscribedEntity(gm,
                                [](Entity, ViewPosition& position, ViewVelocity& velocity)
                                {
                                    position.x += velocity.dx;
                                    position.y += velocity.dy;
                                });
    }
};

Entity makeSubscribedEntity(GeneralManager& gm, float x, float dx)
{
    Entity entity = gm.createEntity();
    gm.addComponent<ViewPosition>(entity, x, 0.0f);
    gm.addComponent<ViewVelocity>(entity, dx, 0.0f);
    gm.subscribeEntity<ViewMovementSystem>(entity);
    return entity;
}
} // namespace

TEST(ComponentView, VisitsOnlySubscribedEntities)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    Entity subscribed = makeSubscribedEntity(gm, 1.0f, 0.0f);

    Entity unsubscribed = gm.createEntity();
    gm.addComponent<ViewPosition>(unsubscribed, 2.0f, 0.0f);
    gm.addComponent<ViewVelocity>(unsubscribed, 0.0f, 0.0f);

    std::vector<Entity> visited;
    gm.forEachSubscribedEntityWith<ViewMovementSystem, ViewPosition, ViewVelocity>(
        [&](Entity entity, ViewPosition&, ViewVelocity&) { visited.push_back(entity); });

    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited[0], subscribed);
}

TEST(ComponentView, JoinFiltersByExtraComponent)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    makeSubscribedEntity(gm, 1.0f, 0.0f);
    Entity withBlob = makeSubscribedEntity(gm, 2.0f, 0.0f);
    gm.addComponent<ViewRareBlob>(withBlob, 42);

    // ViewRareBlob is Sparse — exercises the mixed-storage bit-scan path
    std::vector<Entity> visited;
    gm.forEachSubscribedEntityWith<ViewMovementSystem, ViewPosition, ViewRareBlob>(
        [&](Entity entity, ViewPosition&, ViewRareBlob& blob)
        {
            visited.push_back(entity);
            EXPECT_EQ(blob.payload, 42);
        });

    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited[0], withBlob);
}

TEST(ComponentView, DenseRunsVisitEveryEntity)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    // 130 entities: two full 64-slot words (dense fast path) plus a partial word
    constexpr uint32_t entityCount = 130;
    for (uint32_t i = 0; i < entityCount; ++i)
    {
        makeSubscribedEntity(gm, static_cast<float>(i), 0.0f);
    }

    uint32_t visitedCount = 0;
    float sum = 0.0f;
    gm.forEachSubscribedEntityWith<ViewMovementSystem, ViewPosition, ViewVelocity>(
        [&](Entity, ViewPosition& position, ViewVelocity&)
        {
            ++visitedCount;
            sum += position.x;
        });

    EXPECT_EQ(visitedCount, entityCount);
    EXPECT_FLOAT_EQ(sum, 129.0f * 130.0f / 2.0f);
}

TEST(ComponentView, UnsubscribedHoleIsSkipped)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    Entity first = makeSubscribedEntity(gm, 1.0f, 0.0f);
    Entity middle = makeSubscribedEntity(gm, 2.0f, 0.0f);
    Entity last = makeSubscribedEntity(gm, 3.0f, 0.0f);

    gm.unsubscribeEntity<ViewMovementSystem>(middle);

    std::vector<Entity> visited;
    gm.forEachSubscribedEntityWith<ViewMovementSystem, ViewPosition, ViewVelocity>(
        [&](Entity entity, ViewPosition&, ViewVelocity&) { visited.push_back(entity); });

    EXPECT_EQ(visited, (std::vector<Entity>{first, last}));
}

TEST(ComponentView, AutoUnsubscribeDropsEntityFromView)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    Entity kept = makeSubscribedEntity(gm, 1.0f, 0.0f);
    Entity dropped = makeSubscribedEntity(gm, 2.0f, 0.0f);

    gm.removeComponent<ViewVelocity>(dropped);

    std::vector<Entity> visited;
    gm.forEachSubscribedEntityWith<ViewMovementSystem, ViewPosition>(
        [&](Entity entity, ViewPosition&) { visited.push_back(entity); });

    EXPECT_EQ(visited, (std::vector<Entity>{kept}));
}

TEST(ComponentView, DestroyedEntityDropsFromView)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    Entity kept = makeSubscribedEntity(gm, 1.0f, 0.0f);
    Entity destroyed = makeSubscribedEntity(gm, 2.0f, 0.0f);

    gm.destroyEntity(destroyed);

    std::vector<Entity> visited;
    gm.forEachSubscribedEntityWith<ViewMovementSystem, ViewPosition, ViewVelocity>(
        [&](Entity entity, ViewPosition&, ViewVelocity&) { visited.push_back(entity); });

    EXPECT_EQ(visited, (std::vector<Entity>{kept}));
}

TEST(ComponentView, ConstComponentsAreReadable)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();
    makeSubscribedEntity(gm, 7.0f, 3.0f);

    float x = 0.0f;
    float dx = 0.0f;
    gm.forEachSubscribedEntityWith<ViewMovementSystem, const ViewPosition, const ViewVelocity>(
        [&](Entity, const ViewPosition& position, const ViewVelocity& velocity)
        {
            x = position.x;
            dx = velocity.dx;
        });

    EXPECT_FLOAT_EQ(x, 7.0f);
    EXPECT_FLOAT_EQ(dx, 3.0f);
}

TEST(ComponentView, ReconstructedEntityMatchesHandle)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    // Recycle a slot a few times so the live entity has a non-zero generation
    for (int i = 0; i < 3; ++i)
    {
        gm.destroyEntity(gm.createEntity());
    }
    Entity entity = makeSubscribedEntity(gm, 1.0f, 0.0f);
    ASSERT_GT(entity.generation, 0u);

    std::vector<Entity> visited;
    gm.forEachSubscribedEntityWith<ViewMovementSystem, ViewPosition, ViewVelocity>(
        [&](Entity visitedEntity, ViewPosition&, ViewVelocity&) { visited.push_back(visitedEntity); });

    ASSERT_EQ(visited.size(), 1u);
    EXPECT_EQ(visited[0], entity);
}

TEST(ComponentView, WritesThroughViewPersist)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();
    Entity entity = makeSubscribedEntity(gm, 0.0f, 0.0f);

    gm.forEachSubscribedEntityWith<ViewMovementSystem, ViewPosition>(
        [](Entity, ViewPosition& position) { position.x = 99.0f; });

    EXPECT_FLOAT_EQ(gm.getComponent<ViewPosition>(entity)->x, 99.0f);
}

TEST(ComponentView, CrtpHelperDrivesSystemUpdate)
{
    GeneralManager gm;
    gm.registerSystem<IntegrationSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    Entity first = gm.createEntity();
    gm.addComponent<ViewPosition>(first, 0.0f, 0.0f);
    gm.addComponent<ViewVelocity>(first, 1.0f, 0.0f);
    gm.subscribeEntity<IntegrationSystem>(first);

    Entity second = gm.createEntity();
    gm.addComponent<ViewPosition>(second, 10.0f, 0.0f);
    gm.addComponent<ViewVelocity>(second, -2.0f, 0.0f);
    gm.subscribeEntity<IntegrationSystem>(second);

    gm.update();
    gm.update();

    EXPECT_FLOAT_EQ(gm.getComponent<ViewPosition>(first)->x, 2.0f);
    EXPECT_FLOAT_EQ(gm.getComponent<ViewPosition>(second)->x, 6.0f);
}

TEST(ComponentView, SystemWithoutSubscribersIsNoop)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    uint32_t calls = 0;
    gm.forEachSubscribedEntityWith<ViewMovementSystem, ViewPosition, ViewVelocity>(
        [&](Entity, ViewPosition&, ViewVelocity&) { ++calls; });

    EXPECT_EQ(calls, 0u);
}
