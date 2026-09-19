#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <latch>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace Orhescyon;

namespace
{

struct Position { float x, y; };
struct Health { int value; };

TEST(GeneralManager, AddAndGetComponent)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponentImmediate<Position>(e, 3.0f, 4.0f);

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
    gm.destroyEntityImmediate(e);

    EXPECT_EQ(gm.addComponentImmediate<Position>(e, 0.0f, 0.0f), nullptr);
}

TEST(GeneralManager, RemoveComponent)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponentImmediate<Position>(e, 1.0f, 2.0f);
    gm.removeComponentImmediate<Position>(e);

    EXPECT_EQ(gm.getComponent<Position>(e), nullptr);
}

TEST(GeneralManager, DestroyEntityClearsComponents)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponentImmediate<Position>(e, 1.0f, 2.0f);
    gm.addComponentImmediate<Health>(e, 100);
    gm.destroyEntityImmediate(e);

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
    gm.addComponentImmediate<Position>(e, 1.0f, 2.0f);

    EXPECT_TRUE(gm.hasComponent<Position>(e));
}

TEST(GeneralManager, HasComponentReturnsFalseAfterRemove)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponentImmediate<Position>(e, 1.0f, 2.0f);
    gm.removeComponentImmediate<Position>(e);

    EXPECT_FALSE(gm.hasComponent<Position>(e));
}

TEST(GeneralManager, HasComponentOnInactiveEntityReturnsFalse)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponentImmediate<Position>(e, 1.0f, 2.0f);
    gm.destroyEntityImmediate(e);

    EXPECT_FALSE(gm.hasComponent<Position>(e));
}

TEST(GeneralManager, RecycledSlotHasNoStaleComponents)
{
    GeneralManager gm;
    Entity first = gm.createEntity();
    gm.addComponentImmediate<Position>(first, 1.0f, 2.0f);
    gm.addComponentImmediate<Health>(first, 100);
    gm.destroyEntityImmediate(first);

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
    gm.addComponentImmediate<TrackedResource>(e, 5);
    EXPECT_EQ(TrackedResource::aliveCount, 1);

    gm.destroyEntityImmediate(e);
    EXPECT_EQ(TrackedResource::aliveCount, 0);
}

TEST(GeneralManager, ManagerDestructionDestroysComponents)
{
    TrackedResource::aliveCount = 0;
    {
        GeneralManager gm;
        Entity e = gm.createEntity();
        gm.addComponentImmediate<TrackedResource>(e, 5);
        EXPECT_EQ(TrackedResource::aliveCount, 1);
    }
    EXPECT_EQ(TrackedResource::aliveCount, 0);
}

} // namespace

namespace
{
template <StoragePolicy Policy, uint32_t BlockSize>
struct alignas(64) ParallelComponent
{
    static inline std::atomic<int> liveObjects{0};
    uint64_t value;
    uint64_t checksum;
    std::unique_ptr<uint64_t> ownedValue;

    explicit ParallelComponent(uint64_t initial)
        : value(initial), checksum(~initial), ownedValue(std::make_unique<uint64_t>(initial))
    {
        liveObjects.fetch_add(1, std::memory_order_relaxed);
    }

    ParallelComponent(ParallelComponent&& other) noexcept
        : value(other.value), checksum(other.checksum), ownedValue(std::move(other.ownedValue))
    {
        liveObjects.fetch_add(1, std::memory_order_relaxed);
    }

    ParallelComponent& operator=(ParallelComponent&&) noexcept = default;

    ~ParallelComponent()
    {
        liveObjects.fetch_sub(1, std::memory_order_relaxed);
    }
};

template <StoragePolicy Policy>
struct ParallelTag
{
};

template <StoragePolicy Policy>
struct EmptyTrackedParallelComponent
{
    static inline std::atomic<int> liveObjects{0};
    EmptyTrackedParallelComponent() { liveObjects.fetch_add(1); }
    EmptyTrackedParallelComponent(EmptyTrackedParallelComponent&&) noexcept { liveObjects.fetch_add(1); }
    EmptyTrackedParallelComponent& operator=(EmptyTrackedParallelComponent&&) noexcept = default;
    ~EmptyTrackedParallelComponent() { liveObjects.fetch_sub(1); }
};

template <size_t Index>
struct ParallelRegisteredComponent
{
    static constexpr auto orhescyonStoragePolicy = Index % 2 == 0 ? StoragePolicy::Column : StoragePolicy::Sparse;
    uint64_t value;
};
} // namespace

template <Orhescyon::StoragePolicy Policy, uint32_t BlockSize>
struct Orhescyon::ComponentStorageTraits<ParallelComponent<Policy, BlockSize>>
{
    static constexpr StoragePolicy policy = Policy;
    static constexpr uint32_t blockSize = BlockSize;
};

template <Orhescyon::StoragePolicy Policy>
struct Orhescyon::ComponentStorageTraits<ParallelTag<Policy>>
{
    static constexpr StoragePolicy policy = Policy;
    static constexpr uint32_t blockSize = 64;
};

template <Orhescyon::StoragePolicy Policy>
struct Orhescyon::ComponentStorageTraits<EmptyTrackedParallelComponent<Policy>>
{
    static constexpr StoragePolicy policy = Policy;
    static constexpr uint32_t blockSize = 64;
};

namespace
{
template <typename Func>
void runAdditionThreads(size_t threadCount, Func&& func)
{
    // Report worker failures on the test thread after every worker has finished.
    std::latch ready(static_cast<std::ptrdiff_t>(threadCount));
    std::latch start(1);
    std::vector<std::exception_ptr> failures(threadCount);
    std::vector<std::jthread> workers;
    workers.reserve(threadCount);
    try
    {
        for (size_t worker = 0; worker < threadCount; ++worker)
        {
            workers.emplace_back([&, worker]
            {
                ready.count_down();
                start.wait();
                try { func(worker); }
                catch (...) { failures[worker] = std::current_exception(); }
            });
        }
    }
    catch (...)
    {
        start.count_down();
        throw;
    }
    ready.wait();
    start.count_down();
    for (auto& worker : workers) worker.join();
    for (size_t worker = 0; worker < threadCount; ++worker)
    {
        if (!failures[worker]) continue;
        try { std::rethrow_exception(failures[worker]); }
        catch (const std::exception& error) { ADD_FAILURE() << "Worker " << worker << ": " << error.what(); }
        catch (...) { ADD_FAILURE() << "Worker " << worker << " threw an unknown exception"; }
    }
}

std::vector<Entity> makeAdditionEntities(GeneralManager& manager, size_t count)
{
    std::vector<Entity> entities;
    entities.reserve(count);
    for (size_t i = 0; i < count; ++i) entities.push_back(manager.createEntity());
    return entities;
}

template <typename Component>
void expectAddedComponent(GeneralManager& manager, Entity entity, uint64_t value, Component* expected)
{
    SCOPED_TRACE(entity.slot);
    EXPECT_TRUE(manager.hasComponent<Component>(entity));
    Component* component = manager.getComponent<Component>(entity);
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(component, expected);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(component) % alignof(Component), 0u);
    EXPECT_EQ(component->value, value);
    EXPECT_EQ(component->checksum, ~value);
    ASSERT_NE(component->ownedValue, nullptr);
    EXPECT_EQ(*component->ownedValue, value);
}

template <typename Component>
class ParallelComponentAddition : public testing::Test
{
    void TearDown() override
    {
        EXPECT_EQ(Component::liveObjects.load(std::memory_order_relaxed), 0);
    }
};

using ParallelComponentTypes = testing::Types<
    ParallelComponent<StoragePolicy::Column, 64>, ParallelComponent<StoragePolicy::Sparse, 64>,
    ParallelComponent<StoragePolicy::Column, 4096>, ParallelComponent<StoragePolicy::Sparse, 4096>>;
TYPED_TEST_SUITE(ParallelComponentAddition, ParallelComponentTypes);

TYPED_TEST(ParallelComponentAddition, FirstAddsShareOnePresenceWord)
{
    for (uint64_t round = 0; round < 8; ++round)
    {
        GeneralManager manager(nullptr);
        const auto entities = makeAdditionEntities(manager, 64);
        std::array<TypeParam*, 64> pointers{};
        const size_t threadCount = round % 2 == 0 ? 2 : 8;
        runAdditionThreads(threadCount, [&](size_t worker)
        {
            for (size_t i = worker; i < entities.size(); i += threadCount)
                pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], round * 64 + i);
        });
        ASSERT_FALSE(this->HasFailure());
        for (size_t i = 0; i < entities.size(); ++i)
            expectAddedComponent(manager, entities[i], round * 64 + i, pointers[i]);
        EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, 64u);
        EXPECT_EQ(manager.storageStatistics<TypeParam>().allocatedBlockCount, 1u);
        EXPECT_EQ(TypeParam::liveObjects.load(), 64);
    }
}

TYPED_TEST(ParallelComponentAddition, GrowthPreservesEveryAddressAndValue)
{
    constexpr size_t blockSize = ComponentStorageTraits<TypeParam>::blockSize;
    constexpr size_t count = 4 * blockSize + 65;
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, count);
    std::vector<TypeParam*> pointers(count);
    pointers[0] = manager.addComponentImmediate<TypeParam>(entities[0], uint64_t{0});
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker + 1; i < count; i += 8)
            pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < count; ++i)
        expectAddedComponent(manager, entities[i], i, pointers[i]);
    EXPECT_EQ(std::unordered_set<TypeParam*>(pointers.begin(), pointers.end()).size(), count);
    const auto stats = manager.storageStatistics<TypeParam>();
    EXPECT_EQ(stats.liveComponentCount, count);
    EXPECT_EQ(stats.allocatedBlockCount, (count + blockSize - 1) / blockSize);
    EXPECT_EQ(stats.liveComponentBytes, count * sizeof(TypeParam));
    EXPECT_EQ(stats.allocatedComponentBytes, stats.allocatedBlockCount * blockSize * sizeof(TypeParam));
}

TYPED_TEST(ParallelComponentAddition, ScatteredSlotsCrossWordBlockAndTableBoundaries)
{
    const std::vector<uint32_t> slots{
        0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 65, 127, 128, 129,
        255, 256, 4095, 4096, 4097, 8191, 8192, 16383, 16384, 32767, 32768};
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, slots.back() + 2);
    std::vector<TypeParam*> pointers(slots.size());
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < slots.size(); i += 8)
        {
            const size_t reverse = slots.size() - 1 - i;
            pointers[reverse] = manager.addComponentImmediate<TypeParam>(entities[slots[reverse]], uint64_t{slots[reverse]});
        }
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < slots.size(); ++i)
        expectAddedComponent(manager, entities[slots[i]], slots[i], pointers[i]);
    const std::unordered_set<uint32_t> occupied(slots.begin(), slots.end());
    for (const Entity entity : entities)
        EXPECT_EQ(manager.hasComponent<TypeParam>(entity), occupied.contains(entity.slot));
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, slots.size());
}

TYPED_TEST(ParallelComponentAddition, EntityCreationAndAdditionCanOverlap)
{
    constexpr size_t count = 1024;
    GeneralManager manager(nullptr);
    std::vector<Entity> entities(count);
    std::vector<TypeParam*> pointers(count);
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
        {
            entities[i] = manager.createEntity();
            pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
        }
    });
    ASSERT_FALSE(this->HasFailure());
    std::unordered_set<uint32_t> slots;
    for (size_t i = 0; i < count; ++i)
    {
        EXPECT_TRUE(manager.isActive(entities[i]));
        EXPECT_TRUE(slots.insert(entities[i].slot).second);
        expectAddedComponent(manager, entities[i], i, pointers[i]);
    }
    EXPECT_EQ(manager.activeEntityCount(), count);
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, count);
}

TYPED_TEST(ParallelComponentAddition, OverwritesAndNewAddsOnDifferentEntitiesKeepCountsAndAddresses)
{
    constexpr size_t count = 257;
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, count * 2);
    std::vector<TypeParam*> pointers(count * 2);
    for (size_t i = 0; i < count; ++i)
        pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
    std::vector<TypeParam*> replaced(count);
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
        {
            replaced[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i + 1000});
            pointers[count + i] = manager.addComponentImmediate<TypeParam>(entities[count + i], uint64_t{count + i});
        }
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < count; ++i)
    {
        EXPECT_EQ(replaced[i], pointers[i]);
        expectAddedComponent(manager, entities[i], i + 1000, pointers[i]);
        expectAddedComponent(manager, entities[count + i], count + i, pointers[count + i]);
    }
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, count * 2);
    EXPECT_EQ(TypeParam::liveObjects.load(), count * 2);
}

TYPED_TEST(ParallelComponentAddition, ReadersObserveOnlyCompleteNewComponentsDuringGrowth)
{
    // Visibility must come from component presence, without an extra publication flag in the test.
    constexpr size_t count = 1024;
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, count);
    std::vector<TypeParam*> pointers(count);
    std::array<size_t, 2> invalidReads{};
    runAdditionThreads(8, [&](size_t worker)
    {
        if (worker < 6)
        {
            for (size_t i = worker; i < count; i += 6)
                pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
        }
        else
        {
            for (size_t pass = 0; pass < 32; ++pass)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    if (!manager.hasComponent<TypeParam>(entities[i])) continue;
                    const TypeParam* component = manager.getComponent<TypeParam>(entities[i]);
                    if (!component || component->value != i || component->checksum != ~uint64_t{i}
                        || !component->ownedValue || *component->ownedValue != i)
                        ++invalidReads[worker - 6];
                }
                const auto stats = manager.storageStatistics<TypeParam>();
                if (stats.liveComponentCount > count || stats.liveComponentBytes > count * sizeof(TypeParam))
                    ++invalidReads[worker - 6];
            }
        }
    });
    ASSERT_FALSE(this->HasFailure());
    EXPECT_EQ(invalidReads[0], 0u);
    EXPECT_EQ(invalidReads[1], 0u);
    for (size_t i = 0; i < count; ++i)
        expectAddedComponent(manager, entities[i], i, pointers[i]);
}

TYPED_TEST(ParallelComponentAddition, RemovedComponentsCanBeAddedAgainWithoutLosingSurvivors)
{
    constexpr size_t count = 513;
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, count);
    std::vector<TypeParam*> pointers(count);
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
            pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
    });
    ASSERT_FALSE(this->HasFailure());
    const auto blocksBefore = manager.storageStatistics<TypeParam>().allocatedBlockCount;
    for (size_t round = 1; round <= 3; ++round)
    {
        std::unordered_set<TypeParam*> freed;
        for (size_t i = 0; i < count; i += 2)
        {
            freed.insert(pointers[i]);
            manager.removeComponentImmediate<TypeParam>(entities[i]);
            EXPECT_FALSE(manager.hasComponent<TypeParam>(entities[i]));
            EXPECT_EQ(manager.getComponent<TypeParam>(entities[i]), nullptr);
        }
        EXPECT_EQ(TypeParam::liveObjects.load(), count / 2);
        runAdditionThreads(8, [&](size_t worker)
        {
            for (size_t i = worker * 2; i < count; i += 16)
                pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i + round * count});
        });
        ASSERT_FALSE(this->HasFailure());
        std::unordered_set<TypeParam*> reused;
        for (size_t i = 0; i < count; ++i)
        {
            expectAddedComponent(manager, entities[i], i % 2 == 0 ? i + round * count : i, pointers[i]);
            if (i % 2 == 0)
            {
                EXPECT_TRUE(freed.contains(pointers[i]));
                EXPECT_TRUE(reused.insert(pointers[i]).second);
            }
        }
        EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, count);
        EXPECT_EQ(manager.storageStatistics<TypeParam>().allocatedBlockCount, blocksBefore);
        EXPECT_EQ(TypeParam::liveObjects.load(), count);
    }
}

TYPED_TEST(ParallelComponentAddition, RecycledEntitiesRejectOldHandlesDuringNewAdds)
{
    constexpr size_t count = 32;
    GeneralManager manager(nullptr);
    const auto oldEntities = makeAdditionEntities(manager, count);
    for (size_t i = 0; i < count; ++i)
        manager.addComponentImmediate<TypeParam>(oldEntities[i], uint64_t{i});
    for (Entity entity : oldEntities) manager.destroyEntityImmediate(entity);
    EXPECT_EQ(TypeParam::liveObjects.load(), 0);
    const auto entities = makeAdditionEntities(manager, count);
    std::array<TypeParam*, count> pointers{};
    std::array<TypeParam*, count> staleResults{};
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
        {
            staleResults[i] = manager.addComponentImmediate<TypeParam>(oldEntities[i], uint64_t{9999});
            pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
        }
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < count; ++i)
    {
        EXPECT_EQ(staleResults[i], nullptr);
        EXPECT_FALSE(manager.isActive(oldEntities[i]));
        EXPECT_EQ(entities[i].generation, 1u);
        expectAddedComponent(manager, entities[i], i, pointers[i]);
    }
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, count);
}

TYPED_TEST(ParallelComponentAddition, SeparateManagersKeepTheirStoragesIndependent)
{
    constexpr size_t count = 129;
    GeneralManager first(nullptr);
    GeneralManager second(nullptr);
    const auto firstEntities = makeAdditionEntities(first, count);
    const auto secondEntities = makeAdditionEntities(second, count);
    std::array<TypeParam*, count> firstPointers{};
    std::array<TypeParam*, count> secondPointers{};
    runAdditionThreads(8, [&](size_t worker)
    {
        GeneralManager& manager = worker % 2 == 0 ? first : second;
        const auto& entities = worker % 2 == 0 ? firstEntities : secondEntities;
        auto& pointers = worker % 2 == 0 ? firstPointers : secondPointers;
        for (size_t i = worker / 2; i < count; i += 4)
            pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i + (worker % 2) * count});
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < count; ++i)
    {
        expectAddedComponent(first, firstEntities[i], i, firstPointers[i]);
        expectAddedComponent(second, secondEntities[i], i + count, secondPointers[i]);
        EXPECT_NE(firstPointers[i], secondPointers[i]);
    }
    EXPECT_EQ(first.storageStatistics<TypeParam>().liveComponentCount, count);
    EXPECT_EQ(second.storageStatistics<TypeParam>().liveComponentCount, count);
}

TEST(GeneralManagerParallelAddition, FirstRegistrationOfManyTypesOnTheSameEntities)
{
    constexpr size_t typeCount = 16;
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, 130);
    [&]<size_t... Index>(std::index_sequence<Index...>)
    {
        const std::array<void (*)(GeneralManager&, const std::vector<Entity>&), typeCount> adders{
            +[](GeneralManager& target, const std::vector<Entity>& targets)
            {
                for (size_t i = 0; i < targets.size(); ++i)
                    target.addComponentImmediate<ParallelRegisteredComponent<Index>>(targets[i], uint64_t{Index * 1000 + i});
            }...};
        runAdditionThreads(typeCount, [&](size_t worker) { adders[worker](manager, entities); });
        ASSERT_FALSE(testing::Test::HasFailure());
        auto verify = [&]<size_t TypeIndex>()
        {
            using Component = ParallelRegisteredComponent<TypeIndex>;
            EXPECT_EQ(manager.storageStatistics<Component>().liveComponentCount, entities.size());
            for (size_t i = 0; i < entities.size(); ++i)
            {
                const auto* component = manager.getComponent<Component>(entities[i]);
                ASSERT_NE(component, nullptr);
                EXPECT_EQ(component->value, TypeIndex * 1000 + i);
            }
        };
        (verify.template operator()<Index>(), ...);
        for (Entity entity : entities) manager.destroyEntityImmediate(entity);
        auto verifyRemoved = [&]<size_t TypeIndex>()
        {
            EXPECT_EQ(manager.storageStatistics<ParallelRegisteredComponent<TypeIndex>>().liveComponentCount, 0u);
        };
        (verifyRemoved.template operator()<Index>(), ...);
    }(std::make_index_sequence<typeCount>{});
}

template <typename Tag>
class ParallelTagAddition : public testing::Test
{
    void TearDown() override
    {
        if constexpr (requires { Tag::liveObjects; }) EXPECT_EQ(Tag::liveObjects.load(), 0);
    }
};
using ParallelTagTypes = testing::Types<ParallelTag<StoragePolicy::Column>, ParallelTag<StoragePolicy::Sparse>,
    EmptyTrackedParallelComponent<StoragePolicy::Column>, EmptyTrackedParallelComponent<StoragePolicy::Sparse>>;
TYPED_TEST_SUITE(ParallelTagAddition, ParallelTagTypes);

TYPED_TEST(ParallelTagAddition, RepeatedAddsAndReaddsKeepOneComponentPerEntity)
{
    constexpr size_t count = 1025;
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, count);
    std::vector<TypeParam*> pointers(count);
    for (size_t round = 0; round < 3; ++round)
    {
        runAdditionThreads(8, [&](size_t worker)
        {
            for (size_t i = worker; i < count; i += 8)
            {
                pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i]);
                manager.addComponentImmediate<TypeParam>(entities[i]);
            }
        });
        ASSERT_FALSE(this->HasFailure());
        for (size_t i = 0; i < count; ++i)
        {
            ASSERT_NE(pointers[i], nullptr);
            EXPECT_TRUE(manager.hasComponent<TypeParam>(entities[i]));
            EXPECT_EQ(manager.getComponent<TypeParam>(entities[i]), pointers[i]);
        }
        const auto stats = manager.storageStatistics<TypeParam>();
        EXPECT_EQ(stats.liveComponentCount, count);
        if constexpr (ComponentStorageTraits<TypeParam>::policy == StoragePolicy::Column
                      && std::is_trivially_destructible_v<TypeParam>)
        {
            EXPECT_EQ(stats.allocatedBlockCount, 0u);
            EXPECT_EQ(stats.allocatedComponentBytes, 0u);
        }
        else
        {
            EXPECT_EQ(stats.allocatedBlockCount, (count + 63) / 64);
        }
        if constexpr (requires { TypeParam::liveObjects; }) EXPECT_EQ(TypeParam::liveObjects.load(), count);
        for (Entity entity : entities) manager.removeComponentImmediate<TypeParam>(entity);
        EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, 0u);
        if constexpr (requires { TypeParam::liveObjects; }) EXPECT_EQ(TypeParam::liveObjects.load(), 0);
    }
}

TYPED_TEST(ParallelComponentAddition, ReserveCanOverlapAddsWithoutReplacingExistingBlocks)
{
    constexpr size_t blockSize = ComponentStorageTraits<TypeParam>::blockSize;
    constexpr size_t count = blockSize + 65;
    ComponentManager manager;
    auto& storage = manager.getStorage<TypeParam>();
    std::vector<TypeParam*> pointers(count);
    runAdditionThreads(8, [&](size_t worker)
    {
        if (worker < 2)
        {
            const std::array<size_t, 7> capacities{0, 1, 63, 64, blockSize, count, count * 2};
            for (size_t i = 0; i < capacities.size(); ++i)
                storage.reserve(capacities[worker == 0 ? i : capacities.size() - 1 - i]);
        }
        else
        {
            for (size_t i = worker - 2; i < count; i += 6)
                pointers[i] = manager.addComponent<TypeParam>(Entity{static_cast<uint32_t>(i), 0}, uint64_t{i});
        }
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < count; ++i)
    {
        const auto* component = storage.getComponent(Entity{static_cast<uint32_t>(i), 0});
        ASSERT_NE(component, nullptr);
        EXPECT_EQ(component, pointers[i]);
        EXPECT_EQ(component->value, i);
    }
    const auto stats = storage.statistics();
    EXPECT_EQ(storage.size(), count);
    EXPECT_EQ(stats.liveComponentCount, count);
    const size_t allocatedSlots = ComponentStorageTraits<TypeParam>::policy == StoragePolicy::Column ? count * 2 : count;
    EXPECT_EQ(stats.allocatedBlockCount, (allocatedSlots + blockSize - 1) / blockSize);
    storage.reserve(0);
    storage.reserve(1);
    EXPECT_EQ(storage.statistics().allocatedBlockCount, stats.allocatedBlockCount);
    EXPECT_EQ(storage.size(), count);
}

TYPED_TEST(ParallelTagAddition, ReserveAndAddsAllocateOnlyTheRequiredTagStorage)
{
    constexpr uint32_t count = 1025;
    ComponentManager manager;
    auto& storage = manager.getStorage<TypeParam>();
    runAdditionThreads(8, [&](size_t worker)
    {
        if (worker == 0)
        {
            storage.reserve(count * 2);
            storage.reserve(0);
            storage.reserve(1);
        }
        else
        {
            for (uint32_t slot = static_cast<uint32_t>(worker - 1); slot < count; slot += 7)
                manager.addComponent<TypeParam>(Entity{slot, 0});
        }
    });
    ASSERT_FALSE(this->HasFailure());
    for (uint32_t slot = 0; slot < count; ++slot)
        EXPECT_TRUE(storage.hasComponent(Entity{slot, 0}));
    const auto stats = storage.statistics();
    EXPECT_EQ(storage.size(), count);
    EXPECT_EQ(stats.liveComponentCount, count);
    if constexpr (ComponentStorageTraits<TypeParam>::policy == StoragePolicy::Column)
    {
        if constexpr (std::is_trivially_destructible_v<TypeParam>) EXPECT_EQ(stats.allocatedBlockCount, 0u);
        else EXPECT_EQ(stats.allocatedBlockCount, (count * 2 + 63) / 64);
    }
    else
        EXPECT_EQ(stats.allocatedBlockCount, (count + 63) / 64);
}

template <typename Component>
class ParallelAddedSystem : public SystemCore<ParallelAddedSystem<Component>, Component>
{
public:
    void update(GeneralManager&) override {}
};

TYPED_TEST(ParallelComponentAddition, ViewsAndSubscriptionsRemainCorrectAfterParallelAdds)
{
    constexpr size_t count = 130;
    GeneralManager manager(nullptr);
    manager.registerSystem<ParallelAddedSystem<TypeParam>>();
    const auto entities = makeAdditionEntities(manager, count);
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
            manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
    });
    ASSERT_FALSE(this->HasFailure());
    for (Entity entity : entities) manager.subscribeEntityImmediate<ParallelAddedSystem<TypeParam>>(entity);
    auto verifyView = [&](bool allSubscribed)
    {
        std::array<size_t, count> visits{};
        manager.forEachSubscribedEntityWith<ParallelAddedSystem<TypeParam>, TypeParam>(
            [&](Entity entity, TypeParam& component)
            {
                ASSERT_LT(entity.slot, count);
                ++visits[entity.slot];
                EXPECT_EQ(entity, entities[entity.slot]);
                EXPECT_EQ(component.value, entity.slot);
            });
        for (size_t i = 0; i < count; ++i)
            EXPECT_EQ(visits[i], allSubscribed || i % 3 != 0 ? 1u : 0u);
    };
    verifyView(true);
    for (size_t i = 0; i < count; i += 3) manager.removeComponentImmediate<TypeParam>(entities[i]);
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker * 3; i < count; i += 24)
            manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
    });
    ASSERT_FALSE(this->HasFailure());
    verifyView(false);
    for (size_t i = 0; i < count; i += 3) manager.subscribeEntityImmediate<ParallelAddedSystem<TypeParam>>(entities[i]);
    verifyView(true);
}

enum class AdditionFailurePoint { None, Constructor, Move, Assignment };

struct ExpectedAdditionFailure
{
    AdditionFailurePoint point;
};

struct AdditionConstructionGate
{
    std::latch entered{1};
    std::latch finish{1};
    std::atomic<bool> announced{false};

    void announce()
    {
        if (!announced.exchange(true)) entered.count_down();
    }
};

template <StoragePolicy Policy>
struct ControlledAdditionComponent
{
    static inline std::atomic<int> liveObjects{0};
    uint64_t value;
    uint64_t checksum;
    std::unique_ptr<uint64_t> ownedValue;
    AdditionFailurePoint failure;
    AdditionConstructionGate* gate;

    explicit ControlledAdditionComponent(uint64_t initial, AdditionFailurePoint point = AdditionFailurePoint::None,
                                         AdditionConstructionGate* constructionGate = nullptr)
        : value(initial), checksum(~initial), ownedValue(std::make_unique<uint64_t>(initial)),
          failure(point), gate(constructionGate)
    {
        if (failure == AdditionFailurePoint::Constructor) throw ExpectedAdditionFailure{failure};
        liveObjects.fetch_add(1);
    }

    ControlledAdditionComponent(ControlledAdditionComponent&& other)
        : value(other.value), failure(other.failure), gate(other.gate)
    {
        if (failure == AdditionFailurePoint::Move) throw ExpectedAdditionFailure{failure};
        ownedValue = std::move(other.ownedValue);
        if (gate)
        {
            gate->announce();
            gate->finish.wait();
        }
        checksum = ~value;
        liveObjects.fetch_add(1);
    }

    ControlledAdditionComponent& operator=(ControlledAdditionComponent&& other)
    {
        if (other.failure == AdditionFailurePoint::Assignment) throw ExpectedAdditionFailure{other.failure};
        value = other.value;
        checksum = other.checksum;
        ownedValue = std::move(other.ownedValue);
        failure = other.failure;
        gate = other.gate;
        return *this;
    }

    ~ControlledAdditionComponent() { liveObjects.fetch_sub(1); }
};
} // namespace

template <Orhescyon::StoragePolicy Policy>
struct Orhescyon::ComponentStorageTraits<ControlledAdditionComponent<Policy>>
{
    static constexpr StoragePolicy policy = Policy;
    static constexpr uint32_t blockSize = 64;
};

namespace
{
template <typename Component>
class ExceptionalParallelAddition : public testing::Test
{
    void TearDown() override { EXPECT_EQ(Component::liveObjects.load(), 0); }
};
using ControlledAdditionTypes = testing::Types<ControlledAdditionComponent<StoragePolicy::Column>,
                                             ControlledAdditionComponent<StoragePolicy::Sparse>>;
TYPED_TEST_SUITE(ExceptionalParallelAddition, ControlledAdditionTypes);

TYPED_TEST(ExceptionalParallelAddition, ConstructionFailuresDoNotPublishAndCanBeRetried)
{
    constexpr size_t count = 192;
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, count);
    std::array<TypeParam*, count> pointers{};
    std::array<AdditionFailurePoint, count> failures{};
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
        {
            const auto point = i % 3 == 0 ? AdditionFailurePoint::Constructor
                             : i % 3 == 1 ? AdditionFailurePoint::Move : AdditionFailurePoint::None;
            try { pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i}, point); }
            catch (const ExpectedAdditionFailure& error) { failures[i] = error.point; }
        }
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < count; ++i)
    {
        if (i % 3 == 2)
        {
            EXPECT_EQ(failures[i], AdditionFailurePoint::None);
            expectAddedComponent(manager, entities[i], i, pointers[i]);
        }
        else
        {
            EXPECT_EQ(failures[i], i % 3 == 0 ? AdditionFailurePoint::Constructor : AdditionFailurePoint::Move);
            EXPECT_EQ(pointers[i], nullptr);
            EXPECT_FALSE(manager.hasComponent<TypeParam>(entities[i]));
            EXPECT_EQ(manager.getComponent<TypeParam>(entities[i]), nullptr);
        }
    }
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, count / 3);
    EXPECT_EQ(TypeParam::liveObjects.load(), count / 3);
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
            if (i % 3 != 2) pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < count; ++i) expectAddedComponent(manager, entities[i], i, pointers[i]);
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, count);
    EXPECT_EQ(TypeParam::liveObjects.load(), count);
}

TYPED_TEST(ExceptionalParallelAddition, FailedMovesReturnRecycledSlotsForConcurrentReuse)
{
    constexpr size_t count = 129;
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, count);
    std::unordered_set<TypeParam*> original;
    for (size_t i = 0; i < count; ++i)
        original.insert(manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i}));
    for (Entity entity : entities) manager.removeComponentImmediate<TypeParam>(entity);
    const auto blocks = manager.storageStatistics<TypeParam>().allocatedBlockCount;
    std::array<size_t, 8> caught{};
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
        {
            try { manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i}, AdditionFailurePoint::Move); }
            catch (const ExpectedAdditionFailure& error)
            {
                if (error.point == AdditionFailurePoint::Move) ++caught[worker];
            }
        }
    });
    ASSERT_FALSE(this->HasFailure());
    size_t failureCount = 0;
    for (size_t failures : caught) failureCount += failures;
    EXPECT_EQ(failureCount, count);
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, 0u);
    EXPECT_EQ(TypeParam::liveObjects.load(), 0);
    for (Entity entity : entities) EXPECT_FALSE(manager.hasComponent<TypeParam>(entity));
    std::array<TypeParam*, count> pointers{};
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
            pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < count; ++i) expectAddedComponent(manager, entities[i], i, pointers[i]);
    EXPECT_EQ(std::unordered_set<TypeParam*>(pointers.begin(), pointers.end()), original);
    EXPECT_EQ(manager.storageStatistics<TypeParam>().allocatedBlockCount, blocks);
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, count);
}

TYPED_TEST(ExceptionalParallelAddition, FailedOverwritesPreserveExistingObjectsAndCounts)
{
    constexpr size_t count = 65;
    GeneralManager manager(nullptr);
    const auto entities = makeAdditionEntities(manager, count);
    std::array<TypeParam*, count> pointers{};
    std::array<AdditionFailurePoint, count> failures{};
    for (size_t i = 0; i < count; ++i)
        pointers[i] = manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{i});
    runAdditionThreads(8, [&](size_t worker)
    {
        for (size_t i = worker; i < count; i += 8)
        {
            try { manager.addComponentImmediate<TypeParam>(entities[i], uint64_t{9999}, AdditionFailurePoint::Assignment); }
            catch (const ExpectedAdditionFailure& error) { failures[i] = error.point; }
        }
    });
    ASSERT_FALSE(this->HasFailure());
    for (size_t i = 0; i < count; ++i)
    {
        EXPECT_EQ(failures[i], AdditionFailurePoint::Assignment);
        expectAddedComponent(manager, entities[i], i, pointers[i]);
    }
    EXPECT_EQ(TypeParam::liveObjects.load(), count);
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, count);
}

TYPED_TEST(ExceptionalParallelAddition, PresenceIsNotPublishedUntilConstructionFinishes)
{
    // Pause before the constructor initializes its last field to check the publication boundary.
    GeneralManager manager(nullptr);
    const Entity entity = manager.createEntity();
    AdditionConstructionGate gate;
    TypeParam* pointer = nullptr;
    std::exception_ptr failure;
    std::jthread writer([&]
    {
        try { pointer = manager.addComponentImmediate<TypeParam>(entity, uint64_t{42}, AdditionFailurePoint::None, &gate); }
        catch (...) { failure = std::current_exception(); gate.announce(); }
    });
    gate.entered.wait();
    EXPECT_FALSE(manager.hasComponent<TypeParam>(entity));
    EXPECT_EQ(manager.getComponent<TypeParam>(entity), nullptr);
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, 0u);
    gate.finish.count_down();
    writer.join();
    ASSERT_FALSE(failure);
    expectAddedComponent(manager, entity, 42, pointer);
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, 1u);
}

TYPED_TEST(ExceptionalParallelAddition, InvalidEntitiesAreRejectedBeforeConstructingTheComponent)
{
    GeneralManager manager(nullptr);
    const Entity stale = manager.createEntity();
    manager.destroyEntityImmediate(stale);
    const Entity fresh = manager.createEntity();
    ASSERT_EQ(fresh.slot, stale.slot);
    const std::array<Entity, 2> invalid{stale, Entity{std::numeric_limits<uint32_t>::max(), 0}};
    for (Entity entity : invalid)
    {
        EXPECT_NO_THROW(EXPECT_EQ(manager.addComponentImmediate<TypeParam>(
            entity, uint64_t{42}, AdditionFailurePoint::Constructor), nullptr));
    }
    EXPECT_EQ(TypeParam::liveObjects.load(), 0);
    EXPECT_EQ(manager.storageStatistics<TypeParam>().liveComponentCount, 0u);
    EXPECT_FALSE(manager.hasComponent<TypeParam>(fresh));
    auto* pointer = manager.addComponentImmediate<TypeParam>(fresh, uint64_t{7});
    expectAddedComponent(manager, fresh, 7, pointer);
}
} // namespace
