#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>
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
    inline static Entity spawned = Entity::invalid();

    std::string_view getSystemManagerName() const override { return "game"; }

    bool spawnDuringUpdate = false;

    explicit SpawnerSystem(bool spawnDuringUpdate_) : spawnDuringUpdate(spawnDuringUpdate_) {}

    void update(GeneralManager& gm) override
    {
        if (spawnDuringUpdate) spawn(gm);
    }

    void onEntitySubscribed(Entity, GeneralManager& gm) override
    {
        spawn(gm);
    }

private:
    static void spawn(GeneralManager& gm)
    {
        spawned = gm.createEntity();
        gm.addComponentDeferred<Marker>(spawned, "game");
    }
};

template <int TId>
class ParallelSpawnerSystem : public SystemCore<ParallelSpawnerSystem<TId>>
{
public:
    static constexpr int spawnCount = 128;
    inline static std::vector<Entity> entities;

    std::string_view getSystemManagerName() const override { return "game"; }

    void update(GeneralManager& gm) override
    {
        entities.clear();
        entities.reserve(spawnCount);
        for (int i = 0; i < spawnCount; ++i)
        {
            Entity entity = gm.createEntity();
            entities.push_back(entity);
            gm.addComponentDeferred<Marker>(entity, "game");
        }
    }
};

TEST(GeneralManagerDeferred, CreateReturnsActiveEntityImmediately)
{
    GeneralManager gm;

    Entity entity = gm.createEntity();

    EXPECT_TRUE(gm.isActive(entity));
    EXPECT_EQ(gm.activeEntityCount(), 1u);
}

TEST(GeneralManagerDeferred, DestroyByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity e = gm.createEntity();

    gm.destroyEntityDeferred(e, "game");
    EXPECT_TRUE(gm.isActive(e));

    gm.update("game");
    EXPECT_FALSE(gm.isActive(e));
}

TEST(GeneralManagerDeferred, AddComponentByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity e = gm.createEntity();

    gm.addComponentDeferred<Ammo>(e, 5, "game");
    EXPECT_FALSE(gm.hasComponent<Ammo>(e));

    gm.update("game");
    ASSERT_NE(gm.getComponent<Ammo>(e), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(e)->count, 5);
}

TEST(GeneralManagerDeferred, RemoveComponentByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity e = gm.createEntity();
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
    Entity e = gm.createEntity();

    gm.addComponentDeferred<Ammo>(e, 1, "game");
    gm.addComponentDeferred<Ammo>(e, 2, "game");

    gm.update("game");
    EXPECT_EQ(gm.getComponent<Ammo>(e)->count, 2);

    gm.removeComponentDeferred<Ammo>(e, "game");
    gm.addComponentDeferred<Ammo>(e, 7, "game");

    gm.update("game");
    EXPECT_EQ(gm.getComponent<Ammo>(e)->count, 7);
}

TEST(GeneralManagerDeferred, NewEntityCanQueueComponentsAndSubscription)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity entity = gm.createEntity();
    gm.addComponentDeferred<Position>(entity, 1.f, 2.f, "game");
    gm.addComponentDeferred<Velocity>(entity, 3.f, 4.f, "game");
    gm.subscribeEntityDeferred<MovementSystem>(entity, "game");

    EXPECT_TRUE(gm.isActive(entity));
    EXPECT_FALSE(gm.hasComponent<Position>(entity));
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(entity));

    gm.update("game");

    ASSERT_NE(gm.getComponent<Position>(entity), nullptr);
    EXPECT_FLOAT_EQ(gm.getComponent<Position>(entity)->x, 1.f);
    ASSERT_NE(gm.getComponent<Velocity>(entity), nullptr);
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(entity));
}

TEST(GeneralManagerDeferred, QueuesAreIndependentPerSystemManager)
{
    GeneralManager gm;
    gm.registerSystemManager("first");
    gm.registerSystemManager("second");

    Entity first = gm.createEntity();
    Entity second = gm.createEntity();
    gm.addComponentDeferred<Ammo>(first, 1, "first");
    gm.addComponentDeferred<Ammo>(second, 2, "second");

    gm.update("second");
    EXPECT_FALSE(gm.hasComponent<Ammo>(first));
    ASSERT_NE(gm.getComponent<Ammo>(second), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(second)->count, 2);

    gm.update("first");
    ASSERT_NE(gm.getComponent<Ammo>(first), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(first)->count, 1);
}

TEST(GeneralManagerDeferred, StaleTargetSkipsAndQueueContinues)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity doomed = gm.createEntity();
    Entity fresh = gm.createEntity();

    gm.destroyEntityDeferred(doomed, "game");
    gm.addComponentDeferred<Ammo>(doomed, 9, "game");
    gm.addComponentDeferred<Ammo>(fresh, 4, "game");

    gm.update("game");

    EXPECT_FALSE(gm.isActive(doomed));
    ASSERT_NE(gm.getComponent<Ammo>(fresh), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(fresh)->count, 4);
}

TEST(GeneralManagerDeferred, SubscribeByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity entity = gm.createEntity();
    gm.addComponentImmediate<Position>(entity, 1.f, 2.f);
    gm.addComponentImmediate<Velocity>(entity, 3.f, 4.f);

    gm.subscribeEntityDeferred<MovementSystem>(entity, "game");
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(entity));

    gm.update("game");
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(entity));
}

TEST(GeneralManagerDeferred, UnknownSystemManagerDropsEntityCommands)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity destroyTarget = gm.createEntity();
    Entity addTarget = gm.createEntity();
    Entity removeTarget = gm.createEntity();
    gm.addComponentImmediate<Ammo>(removeTarget, 3);

    Entity subscribeTarget = gm.createEntity();
    gm.addComponentImmediate<Position>(subscribeTarget, 0.f, 0.f);
    gm.addComponentImmediate<Velocity>(subscribeTarget, 0.f, 0.f);

    Entity unsubscribeTarget = gm.createEntity();
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

TEST(GeneralManagerDeferred, StaleEntityCommandsAreSkippedAndQueueContinues)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity stale = gm.createEntity();
    gm.destroyEntityImmediate(stale);
    Entity survivor = gm.createEntity();

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

TEST(GeneralManagerDeferred, CreationFromSystemIsImmediateAndDeferredCommandsFlushSameUpdate)
{
    SpawnerSystem::spawned = Entity::invalid();

    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<SpawnerSystem>(true);

    gm.update("game");

    ASSERT_NE(SpawnerSystem::spawned, Entity::invalid());
    EXPECT_TRUE(gm.isActive(SpawnerSystem::spawned));
    EXPECT_TRUE(gm.hasComponent<Marker>(SpawnerSystem::spawned));
}

TEST(GeneralManagerDeferred, CreationFromFlushCallbackIsImmediateAndDeferredCommandsWaitNextUpdate)
{
    SpawnerSystem::spawned = Entity::invalid();

    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<SpawnerSystem>(false);

    Entity host = gm.createEntity();
    gm.subscribeEntityDeferred<SpawnerSystem>(host, "game");

    gm.update("game");
    ASSERT_NE(SpawnerSystem::spawned, Entity::invalid());
    EXPECT_TRUE(gm.isActive(SpawnerSystem::spawned));
    EXPECT_FALSE(gm.hasComponent<Marker>(SpawnerSystem::spawned));

    gm.update("game");
    EXPECT_TRUE(gm.hasComponent<Marker>(SpawnerSystem::spawned));
}

TEST(GeneralManagerDeferred, RemoveRequiredComponentByEntityAutoUnsubscribesAtFlush)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity e = gm.createEntity();
    gm.addComponentImmediate<Position>(e, 0.f, 0.f);
    gm.addComponentImmediate<Velocity>(e, 0.f, 0.f);
    gm.subscribeEntityImmediate<MovementSystem>(e);
    ASSERT_TRUE(gm.isSubscribedTo<MovementSystem>(e));

    gm.removeComponentDeferred<Position>(e, "game");
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(e));

    gm.update("game");
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(e));
}

TEST(GeneralManagerDeferred, UnsubscribeByEntityAppliesAtUpdateEnd)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<MovementSystem>().writes<Position>().reads<Velocity>();

    Entity e = gm.createEntity();
    gm.addComponentImmediate<Position>(e, 0.f, 0.f);
    gm.addComponentImmediate<Velocity>(e, 0.f, 0.f);
    gm.subscribeEntityImmediate<MovementSystem>(e);

    gm.unsubscribeEntityDeferred<MovementSystem>(e, "game");
    EXPECT_TRUE(gm.isSubscribedTo<MovementSystem>(e));

    gm.update("game");
    EXPECT_FALSE(gm.isSubscribedTo<MovementSystem>(e));
}

TEST(GeneralManagerDeferred, DefaultManagerFlushesOnParameterlessUpdate)
{
    GeneralManager gm;
    Entity entity = gm.createEntity();

    gm.addComponentDeferred<Ammo>(entity, 9, "default");
    EXPECT_FALSE(gm.hasComponent<Ammo>(entity));

    gm.update();

    ASSERT_NE(gm.getComponent<Ammo>(entity), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(entity)->count, 9);
}

TEST(GeneralManagerDeferred, FlushedCommandsAreNotAppliedAgain)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity entity = gm.createEntity();

    gm.addComponentDeferred<Ammo>(entity, 1, "game");
    gm.update("game");

    Ammo* ammo = gm.getComponent<Ammo>(entity);
    ASSERT_NE(ammo, nullptr);
    ammo->count = 7;

    gm.update("game");

    ASSERT_NE(gm.getComponent<Ammo>(entity), nullptr);
    EXPECT_EQ(gm.getComponent<Ammo>(entity)->count, 7);
}

TEST(GeneralManagerDeferred, StaleEntityDoesNotTargetRecycledSlot)
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    Entity stale = gm.createEntity();
    gm.destroyEntityDeferred(stale, "game");
    gm.update("game");

    Entity recycled = gm.createEntity();
    ASSERT_EQ(recycled.slot, stale.slot);
    ASSERT_NE(recycled.generation, stale.generation);

    gm.addComponentDeferred<Ammo>(recycled, 2, "game");
    gm.addComponentDeferred<Ammo>(stale, 1, "game");
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

    Entity entity = gm.createEntity();
    gm.addComponentDeferred<Marker>(entity, "game");
    gm.update("game");

    EXPECT_TRUE(gm.hasComponent<Marker>(entity));
}

TEST(GeneralManagerDeferred, AddComponentByEntityOwnsMoveOnlyArgumentsUntilFlush)
{
    GeneralManager gm;
    gm.registerSystemManager("game");
    Entity entity = gm.createEntity();

    auto value = std::make_unique<int>(42);
    gm.addComponentDeferred<OwnedValue>(entity, std::move(value), "game");
    EXPECT_EQ(value, nullptr);

    gm.update("game");

    OwnedValue* component = gm.getComponent<OwnedValue>(entity);
    ASSERT_NE(component, nullptr);
    ASSERT_NE(component->value, nullptr);
    EXPECT_EQ(*component->value, 42);
}

TEST(GeneralManagerDeferred, ParallelSystemsCreateEntitiesAndKeepDeferredCommands)
{
    using FirstSpawner = ParallelSpawnerSystem<0>;
    using SecondSpawner = ParallelSpawnerSystem<1>;

    FirstSpawner::entities.clear();
    SecondSpawner::entities.clear();

    GeneralManager gm;
    gm.registerSystemManager("game");
    gm.registerSystem<FirstSpawner>();
    gm.registerSystem<SecondSpawner>();

    gm.update("game");

    constexpr std::size_t expectedCount = FirstSpawner::spawnCount + SecondSpawner::spawnCount;
    EXPECT_EQ(gm.activeEntityCount(), expectedCount);

    std::unordered_set<Entity> uniqueEntities;
    for (const std::vector<Entity>* entities : {&FirstSpawner::entities, &SecondSpawner::entities})
    {
        ASSERT_EQ(entities->size(), FirstSpawner::spawnCount);
        for (Entity entity : *entities)
        {
            EXPECT_TRUE(uniqueEntities.insert(entity).second);
            EXPECT_TRUE(gm.isActive(entity));
            EXPECT_TRUE(gm.hasComponent<Marker>(entity));
        }
    }
    EXPECT_EQ(uniqueEntities.size(), expectedCount);
}

namespace
{
struct DeterminismSnapshot
{
    std::vector<Entity> entities;
    std::vector<int> ammoValues;
    uint32_t liveCount = 0;

    bool operator==(const DeterminismSnapshot&) const = default;
};

DeterminismSnapshot runDeterminismScript()
{
    GeneralManager gm;
    gm.registerSystemManager("game");

    std::vector<Entity> entities;
    for (int i = 0; i < 6; ++i)
    {
        entities.push_back(gm.createEntity());
        if (i % 2 == 0)
        {
            Entity seed = gm.createEntity();
            gm.destroyEntityDeferred(seed, "game");
        }
        gm.addComponentDeferred<Ammo>(entities.back(), i * 10, "game");
    }
    gm.update("game");

    DeterminismSnapshot snapshot;
    snapshot.entities = entities;
    for (Entity entity : entities)
    {
        const Ammo* ammo = gm.getComponent<Ammo>(entity);
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

    EXPECT_EQ(first.entities, second.entities);
    EXPECT_EQ(first.ammoValues, second.ammoValues);
    EXPECT_EQ(first.ammoValues, (std::vector<int>{0, 10, 20, 30, 40, 50}));
    EXPECT_EQ(first.liveCount, 6u);

    std::unordered_set<Entity> uniqueEntities(first.entities.begin(), first.entities.end());
    EXPECT_EQ(uniqueEntities.size(), 6u);
}

} // namespace
