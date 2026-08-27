#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <memory>
#include <unordered_set>
#include <vector>

using namespace Orhescyon;

namespace
{

struct Position { float x, y; };
struct Velocity { float dx, dy; };
struct Ammo { int count; };
struct Marker {};
struct OwnedValue { std::unique_ptr<int> value; };

class MovementSystem : public SystemCore<MovementSystem, Position, Velocity>
{
public:
    std::string_view getSystemManagerName() const override { return "game"; }

    void update(GeneralManager& gm) override {}
};

class SpawnerSystem : public SystemCore<SpawnerSystem>
{
public:
    std::string_view getSystemManagerName() const override { return "game"; }

    bool spawnDuringUpdate = false;

    explicit SpawnerSystem(bool spawnDuringUpdate_) : spawnDuringUpdate(spawnDuringUpdate_) {}

    // Deferred calls made while systems run land at the end of the SAME update,
    // while this one fires during the flush itself and lands in the NEXT batch.
    void update(GeneralManager& gm) override
    {
        if (spawnDuringUpdate)
        {
            gm.createEntityDeferred("game");
        }
    }

    void onEntitySubscribed(Entity entity, GeneralManager& gm) override
    {
        gm.createEntityDeferred("game");
    }
};

template <int TId>
class ParallelSpawnerSystem : public SystemCore<ParallelSpawnerSystem<TId>>
{
public:
    static constexpr int spawnCount = 128;
    inline static std::vector<DeferredEntity> handles;

    std::string_view getSystemManagerName() const override { return "game"; }

    void update(GeneralManager& gm) override
    {
        handles.clear();
        handles.reserve(spawnCount);
        for (int i = 0; i < spawnCount; ++i)
        {
            handles.push_back(gm.createEntityDeferred("game"));
        }
    }
};

TEST(GeneralManagerDeferred, CreateIsInvisibleUntilUpdate)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    DeferredEntity handle = gm.createEntityDeferred("game");

    EXPECT_EQ(gm.activeEntityCount(), 0u);
    EXPECT_EQ(gm.resolveEntity(handle), Entity::invalid());

    gm.update("game");

    EXPECT_EQ(gm.activeEntityCount(), 1u);
    EXPECT_TRUE(gm.isActive(gm.resolveEntity(handle)));
}

TEST(GeneralManagerDeferred, DestroyByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity e = gm.createEntityImmediate();

    gm.destroyEntityDeferred(e, "game");
    EXPECT_TRUE(gm.isActive(e));

    gm.update("game");
    EXPECT_FALSE(gm.isActive(e));
}

TEST(GeneralManagerDeferred, AddComponentByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity e = gm.createEntityImmediate();

    gm.addComponentDeferred<Ammo>(e, 5, "game");
    EXPECT_FALSE(gm.hasComponent<Ammo>(e));

    gm.update("game");
    ASSERT_NE(gm.getComponent<Ammo>(e), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(e)->count, 5);
}

TEST(GeneralManagerDeferred, AddComponentByTokenAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.update("game");
    Entity entity = gm.resolveEntity(handle);

    gm.addComponentDeferred<Ammo>(handle, 5, "game");
    EXPECT_FALSE(gm.hasComponent<Ammo>(entity));

    gm.update("game");
    ASSERT_NE(gm.getComponent<Ammo>(entity), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(entity)->count, 5);
}

TEST(GeneralManagerDeferred, RemoveComponentByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity e = gm.createEntityImmediate();
    gm.addComponentImmediate<Ammo>(e, 5);

    gm.removeComponentDeferred<Ammo>(e, "game");
    EXPECT_TRUE(gm.hasComponent<Ammo>(e));

    gm.update("game");
    EXPECT_FALSE(gm.hasComponent<Ammo>(e));
}

TEST(GeneralManagerDeferred, CommandsByEntityApplyInRecordOrder)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity e = gm.createEntityImmediate();

    gm.addComponentDeferred<Ammo>(e, 1, "game");
    gm.addComponentDeferred<Ammo>(e, 2, "game");

    gm.update("game");
    EXPECT_EQ(gm.getComponent<Ammo>(e)->count, 2);

    gm.removeComponentDeferred<Ammo>(e, "game");
    gm.addComponentDeferred<Ammo>(e, 7, "game");

    gm.update("game");
    EXPECT_EQ(gm.getComponent<Ammo>(e)->count, 7);
}

TEST(GeneralManagerDeferred, CommandsByTokenApplyInRecordOrder)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.update("game");
    Entity entity = gm.resolveEntity(handle);

    gm.addComponentDeferred<Ammo>(handle, 1, "game");
    gm.addComponentDeferred<Ammo>(handle, 2, "game");

    gm.update("game");
    EXPECT_EQ(gm.getComponent<Ammo>(entity)->count, 2);

    gm.removeComponentDeferred<Ammo>(handle, "game");
    gm.addComponentDeferred<Ammo>(handle, 7, "game");

    gm.update("game");
    EXPECT_EQ(gm.getComponent<Ammo>(entity)->count, 7);
}

TEST(GeneralManagerDeferred, TokenChainCreateAddSubscribe)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.addComponentDeferred<Position>(handle, 1.f, 2.f, "game");
    gm.addComponentDeferred<Velocity>(handle, 3.f, 4.f, "game");
    gm.subscribeEntityDeferred<MovementSystem>(handle, "game");

    gm.update("game");

    Entity e = gm.resolveEntity(handle);
    ASSERT_NE(e, Entity::invalid());
    ASSERT_NE(gm.getComponent<Position>(e), nullptr);
    EXPECT_FLOAT_EQ(gm.getComponent<Position>(e)->x, 1.f);
    ASSERT_NE(gm.getComponent<Velocity>(e), nullptr);
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(e));
}

TEST(GeneralManagerDeferred, QueuesAreIndependentPerSystemManager)
{
    GeneralManager gm;
    gm.registerSystemManager("first");
    gm.registerSystemManager("second");

    DeferredEntity a = gm.createEntityDeferred("first");
    DeferredEntity b = gm.createEntityDeferred("second");

    gm.update("second");
    EXPECT_FALSE(gm.isActive(gm.resolveEntity(a)));
    EXPECT_TRUE(gm.isActive(gm.resolveEntity(b)));

    gm.update("first");
    EXPECT_TRUE(gm.isActive(gm.resolveEntity(a)));
}

TEST(GeneralManagerDeferred, StaleTargetSkipsAndQueueContinues)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity doomed = gm.createEntityImmediate();
    DeferredEntity fresh = gm.createEntityDeferred("game");

    gm.destroyEntityDeferred(doomed, "game");
    gm.addComponentDeferred<Ammo>(doomed, 9, "game");
    gm.addComponentDeferred<Ammo>(fresh, 4, "game");

    gm.update("game");

    EXPECT_FALSE(gm.isActive(doomed));
    Entity freshResolved = gm.resolveEntity(fresh);
    ASSERT_NE(freshResolved, Entity::invalid());
    ASSERT_NE(gm.getComponent<Ammo>(freshResolved), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(freshResolved)->count, 4);
}

TEST(GeneralManagerDeferred, DestroyByTokenAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.update("game");
    Entity e = gm.resolveEntity(handle);
    ASSERT_TRUE(gm.isActive(e));

    gm.destroyEntityDeferred(handle, "game");
    EXPECT_TRUE(gm.isActive(e));

    gm.update("game");

    EXPECT_FALSE(gm.isActive(e));
    EXPECT_NE(gm.resolveEntity(handle), Entity::invalid());
}

TEST(GeneralManagerDeferred, CreateAndDestroyByTokenInSameBatch)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.destroyEntityDeferred(handle, "game");

    gm.update("game");

    Entity resolved = gm.resolveEntity(handle);
    EXPECT_NE(resolved, Entity::invalid());
    EXPECT_FALSE(gm.isActive(resolved));
    EXPECT_EQ(gm.activeEntityCount(), 0u);
}

TEST(GeneralManagerDeferred, RemoveComponentByTokenAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.addComponentDeferred<Ammo>(handle, 5, "game");
    gm.update("game");

    Entity resolved = gm.resolveEntity(handle);
    ASSERT_TRUE(gm.hasComponent<Ammo>(resolved));

    gm.removeComponentDeferred<Ammo>(handle, "game");
    EXPECT_TRUE(gm.hasComponent<Ammo>(resolved));

    gm.update("game");
    EXPECT_FALSE(gm.hasComponent<Ammo>(resolved));
}

TEST(GeneralManagerDeferred, SubscribeByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity entity = gm.createEntityImmediate();
    gm.addComponentImmediate<Position>(entity, 1.f, 2.f);
    gm.addComponentImmediate<Velocity>(entity, 3.f, 4.f);

    gm.subscribeEntityDeferred<MovementSystem>(entity, "game");
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(entity));

    gm.update("game");
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(entity));
}

TEST(GeneralManagerDeferred, SubscribeByTokenAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.addComponentDeferred<Position>(handle, 1.f, 2.f, "game");
    gm.addComponentDeferred<Velocity>(handle, 3.f, 4.f, "game");
    gm.update("game");

    Entity entity = gm.resolveEntity(handle);
    gm.subscribeEntityDeferred<MovementSystem>(handle, "game");
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(entity));

    gm.update("game");
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(entity));
}

TEST(GeneralManagerDeferred, UnsubscribeByTokenAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.addComponentDeferred<Position>(handle, 1.f, 2.f, "game");
    gm.addComponentDeferred<Velocity>(handle, 3.f, 4.f, "game");
    gm.subscribeEntityDeferred<MovementSystem>(handle, "game");
    gm.update("game");

    Entity resolved = gm.resolveEntity(handle);
    ASSERT_TRUE(gm.isSubscribedTo<MovementSystem>(resolved));

    gm.unsubscribeEntityDeferred<MovementSystem>(handle, "game");
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(resolved));

    gm.update("game");
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(resolved));
}

TEST(GeneralManagerDeferred, UnknownSystemManagerDropsCreation)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    DeferredEntity orphan = gm.createEntityDeferred("missing");
    gm.registerSystemManager("missing");
    gm.update("missing");

    EXPECT_EQ(gm.resolveEntity(orphan), Entity::invalid());
    EXPECT_EQ(gm.activeEntityCount(), 0u);
}

TEST(GeneralManagerDeferred, UnknownSystemManagerDropsEntityCommands)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity destroyTarget = gm.createEntityImmediate();
    Entity addTarget = gm.createEntityImmediate();
    Entity removeTarget = gm.createEntityImmediate();
    gm.addComponentImmediate<Ammo>(removeTarget, 3);

    Entity subscribeTarget = gm.createEntityImmediate();
    gm.addComponentImmediate<Position>(subscribeTarget, 0.f, 0.f);
    gm.addComponentImmediate<Velocity>(subscribeTarget, 0.f, 0.f);

    Entity unsubscribeTarget = gm.createEntityImmediate();
    gm.addComponentImmediate<Position>(unsubscribeTarget, 0.f, 0.f);
    gm.addComponentImmediate<Velocity>(unsubscribeTarget, 0.f, 0.f);
    gm.subscribeEntityImmediate<MovementSystem>(unsubscribeTarget);

    gm.destroyEntityDeferred(destroyTarget, "missing");
    gm.addComponentDeferred<Ammo>(addTarget, 9, "missing");
    gm.removeComponentDeferred<Ammo>(removeTarget, "missing");
    gm.subscribeEntityDeferred<MovementSystem>(subscribeTarget, "missing");
    gm.unsubscribeEntityDeferred<MovementSystem>(unsubscribeTarget, "missing");

    gm.registerSystemManager("missing");
    gm.update("missing");

    EXPECT_TRUE(gm.isActive(destroyTarget));
    EXPECT_FALSE(gm.hasComponent<Ammo>(addTarget));
    EXPECT_TRUE(gm.hasComponent<Ammo>(removeTarget));
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(subscribeTarget));
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(unsubscribeTarget));
}

TEST(GeneralManagerDeferred, UnknownSystemManagerDropsTokenCommands)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    DeferredEntity destroyTarget = gm.createEntityDeferred("game");
    DeferredEntity addTarget = gm.createEntityDeferred("game");
    DeferredEntity removeTarget = gm.createEntityDeferred("game");
    DeferredEntity subscribeTarget = gm.createEntityDeferred("game");
    DeferredEntity unsubscribeTarget = gm.createEntityDeferred("game");
    gm.update("game");

    Entity destroyEntity = gm.resolveEntity(destroyTarget);
    Entity addEntity = gm.resolveEntity(addTarget);
    Entity removeEntity = gm.resolveEntity(removeTarget);
    gm.addComponentImmediate<Ammo>(removeEntity, 3);

    Entity subscribeEntity = gm.resolveEntity(subscribeTarget);
    gm.addComponentImmediate<Position>(subscribeEntity, 0.f, 0.f);
    gm.addComponentImmediate<Velocity>(subscribeEntity, 0.f, 0.f);

    Entity unsubscribeEntity = gm.resolveEntity(unsubscribeTarget);
    gm.addComponentImmediate<Position>(unsubscribeEntity, 0.f, 0.f);
    gm.addComponentImmediate<Velocity>(unsubscribeEntity, 0.f, 0.f);
    gm.subscribeEntityImmediate<MovementSystem>(unsubscribeEntity);

    gm.destroyEntityDeferred(destroyTarget, "missing");
    gm.addComponentDeferred<Ammo>(addTarget, 9, "missing");
    gm.removeComponentDeferred<Ammo>(removeTarget, "missing");
    gm.subscribeEntityDeferred<MovementSystem>(subscribeTarget, "missing");
    gm.unsubscribeEntityDeferred<MovementSystem>(unsubscribeTarget, "missing");

    gm.registerSystemManager("missing");
    gm.update("missing");

    EXPECT_TRUE(gm.isActive(destroyEntity));
    EXPECT_FALSE(gm.hasComponent<Ammo>(addEntity));
    EXPECT_TRUE(gm.hasComponent<Ammo>(removeEntity));
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(subscribeEntity));
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(unsubscribeEntity));
}

TEST(GeneralManagerDeferred, StaleEntityCommandsAreSkippedAndQueueContinues)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity stale = gm.createEntityImmediate();
    gm.destroyEntityImmediate(stale);
    Entity survivor = gm.createEntityImmediate();

    gm.destroyEntityDeferred(stale, "game");
    gm.addComponentDeferred<Ammo>(stale, 1, "game");
    gm.removeComponentDeferred<Ammo>(stale, "game");
    gm.subscribeEntityDeferred<MovementSystem>(stale, "game");
    gm.unsubscribeEntityDeferred<MovementSystem>(stale, "game");
    gm.addComponentDeferred<Ammo>(survivor, 7, "game");

    gm.update("game");

    ASSERT_NE(gm.getComponent<Ammo>(survivor), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(survivor)->count, 7);
}

TEST(GeneralManagerDeferred, ForeignTokenCommandsAreSkippedAndQueueContinues)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    DeferredEntity foreign{12345};
    Entity survivor = gm.createEntityImmediate();

    gm.destroyEntityDeferred(foreign, "game");
    gm.addComponentDeferred<Ammo>(foreign, 1, "game");
    gm.removeComponentDeferred<Ammo>(foreign, "game");
    gm.subscribeEntityDeferred<MovementSystem>(foreign, "game");
    gm.unsubscribeEntityDeferred<MovementSystem>(foreign, "game");
    gm.addComponentDeferred<Ammo>(survivor, 7, "game");

    gm.update("game");

    ASSERT_NE(gm.getComponent<Ammo>(survivor), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(survivor)->count, 7);
}

TEST(GeneralManagerDeferred, CrossManagerCommandBeforeCreationIsSkipped)
{
    GeneralManager gm;
    gm.registerSystemManager("first");
    gm.registerSystemManager("second");

    DeferredEntity handle = gm.createEntityDeferred("first");
    gm.addComponentDeferred<Ammo>(handle, 1, "second");

    gm.update("second");
    gm.update("first");

    Entity resolved = gm.resolveEntity(handle);
    ASSERT_TRUE(gm.isActive(resolved));
    EXPECT_FALSE(gm.hasComponent<Ammo>(resolved));

    gm.addComponentDeferred<Ammo>(handle, 2, "second");
    gm.update("second");

    ASSERT_NE(gm.getComponent<Ammo>(resolved), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(resolved)->count, 2);
}

TEST(GeneralManagerDeferred, DeferredCallFromSystemLandsAtSameUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<SpawnerSystem>(true);

    gm.update("game");

    EXPECT_EQ(gm.activeEntityCount(), 1u);
}

TEST(GeneralManagerDeferred, DeferredCallFromEntityFlushCallbackWaitsNextUpdate)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<SpawnerSystem>(false);

    Entity host = gm.createEntityImmediate();
    gm.subscribeEntityDeferred<SpawnerSystem>(host, "game");

    gm.update("game");
    EXPECT_EQ(gm.activeEntityCount(), 1u);

    gm.update("game");
    EXPECT_EQ(gm.activeEntityCount(), 2u);
}

TEST(GeneralManagerDeferred, DeferredCallFromTokenFlushCallbackWaitsNextUpdate)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<SpawnerSystem>(false);

    DeferredEntity host = gm.createEntityDeferred("game");
    gm.subscribeEntityDeferred<SpawnerSystem>(host, "game");

    gm.update("game");
    EXPECT_EQ(gm.activeEntityCount(), 1u);

    gm.update("game");
    EXPECT_EQ(gm.activeEntityCount(), 2u);
}

TEST(GeneralManagerDeferred, RemoveRequiredComponentByEntityAutoUnsubscribesAtFlush)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity e = gm.createEntityImmediate();
    gm.addComponentImmediate<Position>(e, 0.f, 0.f);
    gm.addComponentImmediate<Velocity>(e, 0.f, 0.f);
    gm.subscribeEntityImmediate<MovementSystem>(e);
    ASSERT_TRUE(gm.isSubscribedTo<MovementSystem>(e));

    gm.removeComponentDeferred<Position>(e, "game");
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(e));

    gm.update("game");
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(e));
}

TEST(GeneralManagerDeferred, RemoveRequiredComponentByTokenAutoUnsubscribesAtFlush)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.addComponentDeferred<Position>(handle, 0.f, 0.f, "game");
    gm.addComponentDeferred<Velocity>(handle, 0.f, 0.f, "game");
    gm.subscribeEntityDeferred<MovementSystem>(handle, "game");
    gm.update("game");

    Entity entity = gm.resolveEntity(handle);
    ASSERT_TRUE(gm.isSubscribedTo<MovementSystem>(entity));

    gm.removeComponentDeferred<Position>(handle, "game");
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(entity));

    gm.update("game");
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(entity));
}

TEST(GeneralManagerDeferred, UnsubscribeByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity e = gm.createEntityImmediate();
    gm.addComponentImmediate<Position>(e, 0.f, 0.f);
    gm.addComponentImmediate<Velocity>(e, 0.f, 0.f);
    gm.subscribeEntityImmediate<MovementSystem>(e);

    gm.unsubscribeEntityDeferred<MovementSystem>(e, "game");
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(e));

    gm.update("game");
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(e));
}

TEST(GeneralManagerDeferred, ForeignHandleResolvesInvalid)
{
    GeneralManager gm;

    EXPECT_EQ(gm.resolveEntity(DeferredEntity{12345}), Entity::invalid());
}

TEST(GeneralManagerDeferred, DefaultManagerFlushesOnParameterlessUpdate)
{
    GeneralManager gm;

    DeferredEntity handle = gm.createEntityDeferred("default");
    gm.update();

    EXPECT_EQ(gm.activeEntityCount(), 1u);
    EXPECT_TRUE(gm.isActive(gm.resolveEntity(handle)));
}

TEST(GeneralManagerDeferred, FlushedCommandsAreNotAppliedAgain)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.update("game");

    Entity resolved = gm.resolveEntity(handle);
    ASSERT_TRUE(gm.isActive(resolved));
    ASSERT_EQ(gm.activeEntityCount(), 1u);

    gm.update("game");

    EXPECT_EQ(gm.resolveEntity(handle), resolved);
    EXPECT_EQ(gm.activeEntityCount(), 1u);
}

TEST(GeneralManagerDeferred, StaleTokenDoesNotTargetRecycledSlot)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.update("game");
    Entity stale = gm.resolveEntity(handle);

    gm.destroyEntityDeferred(handle, "game");
    gm.update("game");

    Entity recycled = gm.createEntityImmediate();
    ASSERT_EQ(recycled.slot, stale.slot);
    ASSERT_NE(recycled.generation, stale.generation);

    gm.addComponentDeferred<Ammo>(handle, 1, "game");
    gm.addComponentDeferred<Ammo>(recycled, 2, "game");
    gm.update("game");

    EXPECT_EQ(gm.resolveEntity(handle), stale);
    EXPECT_FALSE(gm.isActive(stale));
    EXPECT_TRUE(gm.isActive(recycled));
    ASSERT_NE(gm.getComponent<Ammo>(recycled), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(recycled)->count, 2);
}

TEST(GeneralManagerDeferred, StaleEntityDoesNotTargetRecycledSlot)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    Entity stale = gm.createEntityImmediate();
    gm.destroyEntityDeferred(stale, "game");
    gm.update("game");

    Entity recycled = gm.createEntityImmediate();
    ASSERT_EQ(recycled.slot, stale.slot);
    ASSERT_NE(recycled.generation, stale.generation);

    gm.addComponentDeferred<Ammo>(stale, 1, "game");
    gm.addComponentDeferred<Ammo>(recycled, 2, "game");
    gm.update("game");

    EXPECT_FALSE(gm.isActive(stale));
    EXPECT_TRUE(gm.isActive(recycled));
    ASSERT_NE(gm.getComponent<Ammo>(recycled), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(recycled)->count, 2);
}

TEST(GeneralManagerDeferred, AddComponentByEntitySupportsNoConstructorArguments)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    Entity entity = gm.createEntityImmediate();
    gm.addComponentDeferred<Marker>(entity, "game");
    gm.update("game");

    EXPECT_TRUE(gm.hasComponent<Marker>(entity));
}

TEST(GeneralManagerDeferred, AddComponentByTokenSupportsNoConstructorArguments)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.addComponentDeferred<Marker>(handle, "game");
    gm.update("game");

    EXPECT_TRUE(gm.hasComponent<Marker>(gm.resolveEntity(handle)));
}

TEST(GeneralManagerDeferred, AddComponentByEntityOwnsMoveOnlyArgumentsUntilFlush)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity entity = gm.createEntityImmediate();

    auto value = std::make_unique<int>(42);
    gm.addComponentDeferred<OwnedValue>(entity, std::move(value), "game");
    EXPECT_EQ(value, nullptr);

    gm.update("game");

    OwnedValue* component = gm.getComponent<OwnedValue>(entity);
    ASSERT_NE(component, nullptr);
    ASSERT_NE(component->value, nullptr);
    EXPECT_EQ(*component->value, 42);
}

TEST(GeneralManagerDeferred, AddComponentByTokenOwnsMoveOnlyArgumentsUntilFlush)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    DeferredEntity handle = gm.createEntityDeferred("game");
    gm.update("game");

    auto value = std::make_unique<int>(42);
    gm.addComponentDeferred<OwnedValue>(handle, std::move(value), "game");
    EXPECT_EQ(value, nullptr);

    gm.update("game");

    OwnedValue* component = gm.getComponent<OwnedValue>(gm.resolveEntity(handle));
    ASSERT_NE(component, nullptr);
    ASSERT_NE(component->value, nullptr);
    EXPECT_EQ(*component->value, 42);
}

TEST(GeneralManagerDeferred, ParallelSystemsKeepAllDeferredCommandsAndTokens)
{
    using FirstSpawner = ParallelSpawnerSystem<0>;
    using SecondSpawner = ParallelSpawnerSystem<1>;

    FirstSpawner::handles.clear();
    SecondSpawner::handles.clear();

    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<FirstSpawner>();
    gm.registerSystem<SecondSpawner>();

    gm.update("game");

    constexpr std::size_t expectedCount = FirstSpawner::spawnCount + SecondSpawner::spawnCount;
    EXPECT_EQ(gm.activeEntityCount(), expectedCount);

    std::unordered_set<uint64_t> tokenIds;
    for (const std::vector<DeferredEntity>* handles : {&FirstSpawner::handles, &SecondSpawner::handles})
    {
        ASSERT_EQ(handles->size(), FirstSpawner::spawnCount);
        for (DeferredEntity handle : *handles)
        {
            EXPECT_NE(handle.id, 0u);
            EXPECT_TRUE(tokenIds.insert(handle.id).second);
            EXPECT_TRUE(gm.isActive(gm.resolveEntity(handle)));
        }
    }
    EXPECT_EQ(tokenIds.size(), expectedCount);
}

namespace
{
struct DeterminismSnapshot
{
    std::vector<Entity> resolved;
    std::vector<int> ammoValues;
    uint32_t liveCount = 0;

    bool operator==(const DeterminismSnapshot&) const = default;
};

DeterminismSnapshot runDeterminismScript()
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    std::vector<DeferredEntity> handles;
    for (int i = 0; i < 6; ++i)
    {
        handles.push_back(gm.createEntityDeferred("game"));
        if (i % 2 == 0)
        {
            // Immediate churn between recordings keeps the slot free-list busy mid-batch.
            Entity seed = gm.createEntityImmediate();
            gm.destroyEntityDeferred(seed, "game");
        }
        gm.addComponentDeferred<Ammo>(handles.back(), i * 10, "game");
    }
    gm.update("game");

    DeterminismSnapshot snapshot;
    for (DeferredEntity handle : handles)
    {
        Entity resolved = gm.resolveEntity(handle);
        snapshot.resolved.push_back(resolved);
        const Ammo* ammo = gm.getComponent<Ammo>(resolved);
        snapshot.ammoValues.push_back(ammo ? ammo->count : -1);
    }
    snapshot.liveCount = gm.activeEntityCount();
    return snapshot;
}
}

TEST(GeneralManagerDeferred, SameScriptProducesIdenticalState)
{
    DeterminismSnapshot first = runDeterminismScript();
    DeterminismSnapshot second = runDeterminismScript();

    EXPECT_EQ(first.resolved, second.resolved);
    EXPECT_EQ(first.ammoValues, second.ammoValues);
    EXPECT_EQ(first.ammoValues, (std::vector<int>{0, 10, 20, 30, 40, 50}));
    EXPECT_EQ(first.liveCount, 6u);

    std::unordered_set<Entity> uniqueEntities(first.resolved.begin(), first.resolved.end());
    EXPECT_EQ(uniqueEntities.size(), 6u);
}

} // namespace
