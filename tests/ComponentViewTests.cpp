#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <algorithm>
#include <atomic>
#include <exception>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>
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

struct ViewTag {};

class ViewJobSystem : public IJobSystem
{
public:
    std::atomic<std::size_t> callCount{0};

    void parallelFor(std::size_t count, const std::function<void(std::size_t)>& body) override
    {
        callCount.fetch_add(1, std::memory_order_relaxed);
        const std::size_t workerCount = std::min(count, std::size_t{4});
        std::vector<std::future<void>> workers;
        for (std::size_t worker = 0; worker < workerCount; ++worker)
        {
            workers.push_back(std::async(std::launch::async, [&, worker]
            {
                for (std::size_t index = worker; index < count; index += workerCount) body(index);
            }));
        }

        std::exception_ptr failure;
        for (auto& worker : workers)
        {
            try
            {
                worker.get();
            }
            catch (...)
            {
                if (!failure) failure = std::current_exception();
            }
        }
        if (failure) std::rethrow_exception(failure);
    }
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

class ParallelIntegrationSystem : public SystemCore<ParallelIntegrationSystem, ViewPosition, ViewVelocity>
{
public:
    std::string_view getSystemManagerName() const override { return "parallel"; }

    void update(GeneralManager& gm) override
    {
        parallelForEachSubscribedEntity(gm, [](Entity, ViewPosition& position, const ViewVelocity& velocity)
        {
            position.x += velocity.dx;
        });
    }
};

class ParallelCountingSystem : public SystemCore<ParallelCountingSystem>
{
    std::atomic<uint32_t>& _visits;

public:
    explicit ParallelCountingSystem(std::atomic<uint32_t>& visits) : _visits(visits) {}

    std::string_view getSystemManagerName() const override { return "parallel"; }

    void update(GeneralManager& gm) override
    {
        parallelForEachSubscribedEntity(gm, [this](Entity)
        {
            _visits.fetch_add(1, std::memory_order_relaxed);
        });
    }
};

Entity makeSubscribedEntity(GeneralManager& gm, float x, float dx)
{
    Entity entity = gm.createEntity();
    gm.addComponentImmediate<ViewPosition>(entity, x, 0.0f);
    gm.addComponentImmediate<ViewVelocity>(entity, dx, 0.0f);
    gm.subscribeEntityImmediate<ViewMovementSystem>(entity);
    return entity;
}
} // namespace

TEST(ComponentView, VisitsOnlySubscribedEntities)
{
    GeneralManager gm;
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    Entity subscribed = makeSubscribedEntity(gm, 1.0f, 0.0f);

    Entity unsubscribed = gm.createEntity();
    gm.addComponentImmediate<ViewPosition>(unsubscribed, 2.0f, 0.0f);
    gm.addComponentImmediate<ViewVelocity>(unsubscribed, 0.0f, 0.0f);

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
    gm.addComponentImmediate<ViewRareBlob>(withBlob, 42);

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

    gm.unsubscribeEntityImmediate<ViewMovementSystem>(middle);

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

    gm.removeComponentImmediate<ViewVelocity>(dropped);

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

    gm.destroyEntityImmediate(destroyed);

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
        gm.destroyEntityImmediate(gm.createEntity());
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
    gm.addComponentImmediate<ViewPosition>(first, 0.0f, 0.0f);
    gm.addComponentImmediate<ViewVelocity>(first, 1.0f, 0.0f);
    gm.subscribeEntityImmediate<IntegrationSystem>(first);

    Entity second = gm.createEntity();
    gm.addComponentImmediate<ViewPosition>(second, 10.0f, 0.0f);
    gm.addComponentImmediate<ViewVelocity>(second, -2.0f, 0.0f);
    gm.subscribeEntityImmediate<IntegrationSystem>(second);

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

TEST(ComponentView, ParallelViewVisitsDenseRunsAndSkipsRemovedSubscriptions)
{
    ViewJobSystem jobs;
    GeneralManager gm(&jobs);
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();
    gm.destroyEntityImmediate(gm.createEntity());

    constexpr uint32_t entityCount = 258;
    std::vector<Entity> entities;
    for (uint32_t i = 0; i < entityCount; ++i)
        entities.push_back(makeSubscribedEntity(gm, static_cast<float>(i), 2.0f));
    ASSERT_GT(entities.front().generation, 0u);

    gm.unsubscribeEntityImmediate<ViewMovementSystem>(entities[64]);
    gm.removeComponentImmediate<ViewVelocity>(entities[130]);
    gm.destroyEntityImmediate(entities[200]);
    Entity replacement = gm.createEntity();
    ASSERT_EQ(replacement.slot, entities[200].slot);
    gm.addComponentImmediate<ViewPosition>(replacement, -1.0f, 0.0f);
    gm.addComponentImmediate<ViewVelocity>(replacement, 2.0f, 0.0f);

    std::vector<std::atomic<uint32_t>> visits(entityCount);
    for (auto& count : visits) count.store(0);
    gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewPosition, const ViewVelocity>(
        [&](Entity entity, ViewPosition& position, auto& velocity)
        {
            static_assert(std::is_const_v<std::remove_reference_t<decltype(velocity)>>);
            EXPECT_EQ(entity, entities[entity.slot]);
            visits[entity.slot].fetch_add(1, std::memory_order_relaxed);
            position.x += velocity.dx;
        });

    EXPECT_EQ(jobs.callCount.load(), 1u);
    for (uint32_t i = 0; i < entityCount; ++i)
    {
        const bool expected = i != 64 && i != 130 && i != 200;
        EXPECT_EQ(visits[i].load(), expected ? 1u : 0u) << "slot " << i;
        if (expected) EXPECT_FLOAT_EQ(gm.getComponent<ViewPosition>(entities[i])->x, static_cast<float>(i) + 2.0f);
    }
    EXPECT_FLOAT_EQ(gm.getComponent<ViewPosition>(replacement)->x, -1.0f);
}

TEST(ComponentView, ParallelJoinSupportsSparseComponentsAndTags)
{
    ViewJobSystem jobs;
    GeneralManager gm(&jobs);
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    constexpr uint32_t entityCount = 130;
    std::vector<Entity> entities;
    for (uint32_t i = 0; i < entityCount; ++i)
    {
        Entity entity = makeSubscribedEntity(gm, 0.0f, 0.0f);
        entities.push_back(entity);
        if (i < 64 || i % 5 == 0) gm.addComponentImmediate<ViewRareBlob>(entity, static_cast<int>(i));
        if (i < 64 || i % 3 != 0) gm.addComponentImmediate<ViewTag>(entity);
    }

    std::vector<std::atomic<uint32_t>> visits(entityCount);
    for (auto& count : visits) count.store(0);
    gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewPosition, ViewRareBlob, const ViewTag>(
        [&](Entity entity, ViewPosition& position, ViewRareBlob& blob, auto& tag)
        {
            static_assert(std::is_const_v<std::remove_reference_t<decltype(tag)>>);
            visits[entity.slot].fetch_add(1, std::memory_order_relaxed);
            position.x = static_cast<float>(blob.payload);
            ++blob.payload;
        });

    for (uint32_t i = 0; i < entityCount; ++i)
    {
        const bool expected = i < 64 || (i % 5 == 0 && i % 3 != 0);
        EXPECT_EQ(visits[i].load(), expected ? 1u : 0u) << "slot " << i;
        if (expected)
        {
            EXPECT_FLOAT_EQ(gm.getComponent<ViewPosition>(entities[i])->x, static_cast<float>(i));
            EXPECT_EQ(gm.getComponent<ViewRareBlob>(entities[i])->payload, static_cast<int>(i) + 1);
        }
    }
}

TEST(ComponentView, ParallelEmptyViewsDoNotScheduleWork)
{
    ViewJobSystem jobs;
    GeneralManager gm(&jobs);
    auto body = [](Entity, ViewPosition&) { ADD_FAILURE() << "Unexpected entity"; };
    gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewPosition>(body);
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();
    gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewPosition>(body);

    Entity entity = makeSubscribedEntity(gm, 0.0f, 0.0f);
    gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewRareBlob>(
        [](Entity, ViewRareBlob&) { ADD_FAILURE() << "Unexpected sparse component"; });
    gm.unsubscribeEntityImmediate<ViewMovementSystem>(entity);
    gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewPosition>(body);

    EXPECT_EQ(jobs.callCount.load(), 0u);
}

TEST(ComponentView, ParallelViewSupportsMoveOnlyCallbacksAndBackendReplacement)
{
    ViewJobSystem firstJobs;
    ViewJobSystem secondJobs;
    GeneralManager gm(&firstJobs);
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();
    Entity entity = makeSubscribedEntity(gm, 1.0f, 0.0f);

    auto body = [delta = std::make_unique<float>(3.0f)](Entity, ViewPosition& position) { position.x += *delta; };
    gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewPosition>(body);
    gm.setJobSystem(&secondJobs);
    gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewPosition>(std::move(body));

    EXPECT_EQ(firstJobs.callCount.load(), 1u);
    EXPECT_EQ(secondJobs.callCount.load(), 1u);
    EXPECT_FLOAT_EQ(gm.getComponent<ViewPosition>(entity)->x, 7.0f);
}

TEST(ComponentView, ParallelViewPropagatesCallbackExceptions)
{
    ViewJobSystem jobs;
    GeneralManager gm(&jobs);
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();
    makeSubscribedEntity(gm, 0.0f, 0.0f);

    EXPECT_THROW((gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewPosition>(
        [](Entity, ViewPosition&) { throw std::runtime_error("entity callback failure"); })), std::runtime_error);
}

TEST(ComponentView, ParallelCrtpHelpersUseNestedExternalJobs)
{
    ViewJobSystem jobs;
    std::atomic<uint32_t> visits{0};
    GeneralManager gm(&jobs);
    gm.registerSystemManager("parallel");
    gm.registerSystem<ParallelIntegrationSystem>().writes<ViewPosition>().reads<ViewVelocity>();
    gm.registerSystem<ParallelCountingSystem>(visits);

    std::vector<Entity> entities;
    for (uint32_t i = 0; i < 130; ++i)
    {
        Entity entity = gm.createEntity();
        entities.push_back(entity);
        gm.addComponentImmediate<ViewPosition>(entity, 0.0f, 0.0f);
        gm.addComponentImmediate<ViewVelocity>(entity, 2.0f, 0.0f);
        gm.subscribeEntityImmediate<ParallelIntegrationSystem>(entity);
        gm.subscribeEntityImmediate<ParallelCountingSystem>(entity);
    }

    gm.update("parallel");
    gm.update("parallel");

    EXPECT_EQ(jobs.callCount.load(), 6u);
    EXPECT_EQ(visits.load(), 260u);
    for (Entity entity : entities) EXPECT_FLOAT_EQ(gm.getComponent<ViewPosition>(entity)->x, 4.0f);
}

TEST(ComponentView, ParallelCrtpHelpersCompleteWithSingleWorkerDefaultJobs)
{
    DefaultJobSystem jobs(1);
    std::atomic<uint32_t> visits{0};
    GeneralManager gm(&jobs);
    gm.registerSystemManager("parallel");
    gm.registerSystem<ParallelIntegrationSystem>().writes<ViewPosition>().reads<ViewVelocity>();
    gm.registerSystem<ParallelCountingSystem>(visits);

    Entity entity = gm.createEntity();
    gm.addComponentImmediate<ViewPosition>(entity, 0.0f, 0.0f);
    gm.addComponentImmediate<ViewVelocity>(entity, 2.0f, 0.0f);
    gm.subscribeEntityImmediate<ParallelIntegrationSystem>(entity);
    gm.subscribeEntityImmediate<ParallelCountingSystem>(entity);

    gm.update("parallel");

    EXPECT_EQ(visits.load(), 1u);
    EXPECT_FLOAT_EQ(gm.getComponent<ViewPosition>(entity)->x, 2.0f);
}

TEST(ComponentView, ParallelViewAllowsCreationAndQueuesStructuralChangesUntilUpdate)
{
    ViewJobSystem jobs;
    GeneralManager gm(&jobs);
    gm.registerSystem<ViewMovementSystem>().writes<ViewPosition>().reads<ViewVelocity>();

    constexpr uint32_t entityCount = 130;
    std::vector<Entity> originals;
    std::vector<Entity> created(entityCount);
    for (uint32_t i = 0; i < entityCount; ++i) originals.push_back(makeSubscribedEntity(gm, 0.0f, 0.0f));

    gm.parallelForEachSubscribedEntityWith<ViewMovementSystem, ViewPosition>(
        [&](Entity entity, ViewPosition&)
        {
            Entity child = gm.createEntity();
            created[entity.slot] = child;
            gm.addComponentDeferred<ViewRareBlob>(child, static_cast<int>(entity.slot));
            gm.destroyEntityDeferred(entity);
        });

    EXPECT_EQ(gm.activeEntityCount(), entityCount * 2);
    for (Entity entity : originals) EXPECT_TRUE(gm.isActive(entity));
    for (Entity entity : created) EXPECT_FALSE(gm.hasComponent<ViewRareBlob>(entity));

    gm.update();

    EXPECT_EQ(gm.activeEntityCount(), entityCount);
    for (uint32_t i = 0; i < entityCount; ++i)
    {
        EXPECT_FALSE(gm.isActive(originals[i]));
        ASSERT_TRUE(gm.isActive(created[i]));
        ASSERT_NE(gm.getComponent<ViewRareBlob>(created[i]), nullptr);
        EXPECT_EQ(gm.getComponent<ViewRareBlob>(created[i])->payload, static_cast<int>(i));
    }
}
