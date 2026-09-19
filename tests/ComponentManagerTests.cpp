#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/Components/ComponentManager.hpp>
#include <gtest/gtest.h>
#include <array>
#include <barrier>
#include <cstddef>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace Orhescyon;

namespace
{
struct Position
{
	float x, y;
};
struct Health 
{
	int value;
};
struct Name 
{
	std::string text;
	int id;
	Name(std::string t, int i) : text(std::move(t)), id(i) {}
};

template <std::size_t Index>
struct RegistryComponent
{
	int value;
};

struct RegisterComponentOnDestruction
{
	ComponentManager& manager;
	Entity target;

	~RegisterComponentOnDestruction()
	{
		manager.addComponent<Health>(target, 42);
	}
};

struct RemovalCallbackComponent
{
	std::unique_ptr<RegisterComponentOnDestruction> callback;
};

TEST(ComponentManager, AddAndGetComponent) 
{
	ComponentManager cm;
	Entity e1{1, 0};

	Position* p = cm.addComponent<Position>(e1, Position{ 1.0f, 2.0f });
	ASSERT_NE(p, nullptr);
	EXPECT_FLOAT_EQ(p->x, 1.0f);
	EXPECT_FLOAT_EQ(p->y, 2.0f);

	Position* p2 = cm.getComponent<Position>(e1);
	ASSERT_EQ(p, p2);
}

TEST(ComponentManager, RemoveEntityClearsAllItsComponents) 
{
	ComponentManager cm;
	Entity e1{1, 0};
	Entity e2{2, 0};

	cm.addComponent<Position>(e1, Position{ 1.0f, 2.0f });
	cm.addComponent<Health>(e1, Health{ 100 });
	cm.addComponent<Position>(e2, Position{ 3.0f, 4.0f });

	cm.removeEntity(e1);

	// Entity 1 components should be gone
	EXPECT_EQ(cm.getComponent<Position>(e1), nullptr);
	EXPECT_EQ(cm.getComponent<Health>(e1), nullptr);

	// Entity 2 components should remain untouched
	Position* p2 = cm.getComponent<Position>(e2);
	ASSERT_NE(p2, nullptr);
	EXPECT_FLOAT_EQ(p2->x, 3.0f);
}

TEST(ComponentManager, GetStorageReturnsCorrectStorage)
{
	ComponentManager cm;
	Entity e1{1, 0};
	Entity e2{2, 0};

	cm.addComponent<Position>(e1, Position{ 1.0f, 2.0f });
	cm.addComponent<Position>(e2, Position{ 3.0f, 4.0f });

	auto& positions = cm.getStorage<Position>();
	EXPECT_EQ(positions.size(), 2u);

	// Check we can retrieve components from the returned storage
	Position* p1 = positions.getComponent(e1);
	ASSERT_NE(p1, nullptr);
	EXPECT_FLOAT_EQ(p1->x, 1.0f);
}

TEST(ComponentManager, PerfectForwardingArguments) 
{
	ComponentManager cm;
	Entity e1{1, 0};

	Name* n = cm.addComponent<Name>(e1, "TestEntity", 42);

	ASSERT_NE(n, nullptr);
	EXPECT_EQ(n->text, "TestEntity");
	EXPECT_EQ(n->id, 42);
}

TEST(ComponentManager, LazyInitializationDoesNotThrow) 
{
	ComponentManager cm;
	Entity e1{1, 0};

	EXPECT_EQ(cm.getComponent<Position>(e1), nullptr);

	EXPECT_NO_THROW(cm.removeComponent<Position>(e1));

	// Getting the storage should return an empty storage
	auto& positions = cm.getStorage<Position>();
	EXPECT_EQ(positions.size(), 0u);
}

TEST(ComponentManager, TypeIsolation)
{
	ComponentManager cm;
	Entity e1{1, 0};

	cm.addComponent<Position>(e1, Position{ 1.0f, 2.0f });

	// Adding Position should not mysteriously add Health
	EXPECT_EQ(cm.getComponent<Health>(e1), nullptr);
}

TEST(ComponentManager, ConcurrentLookupReturnsSameStorage)
{
	constexpr std::size_t threadCount = 8;
	ComponentManager cm;
	std::array<ComponentManager::StorageFor<Position>*, threadCount> storages{};
	std::barrier start(static_cast<std::ptrdiff_t>(threadCount));
	std::vector<std::thread> workers;
	workers.reserve(threadCount);

	for (std::size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
	{
		workers.emplace_back(
		    [&, threadIndex]
		    {
			    start.arrive_and_wait();
			    storages[threadIndex] = &cm.getStorage<Position>();
		    });
	}
	for (std::thread& worker : workers) worker.join();

	auto* expected = &cm.getStorage<Position>();
	for (auto* storage : storages) EXPECT_EQ(storage, expected);

	Entity entity{1, 0};
	cm.addComponent<Position>(entity, Position{1.0f, 2.0f});
	cm.removeEntity(entity);
	EXPECT_FALSE(cm.hasComponent<Position>(entity));
}

TEST(ComponentManager, ConcurrentRegistrationPreservesComponents)
{
	constexpr std::size_t typeCount = 16;
	ComponentManager cm;
	Entity entity{1, 0};
	cm.addComponent<Position>(entity, Position{1.0f, 2.0f});
	std::array<bool, typeCount> validReads{};
	std::barrier start(static_cast<std::ptrdiff_t>(typeCount));
	std::vector<std::thread> workers;
	workers.reserve(typeCount);

	[&]<std::size_t... Index>(std::index_sequence<Index...>)
	{
		(workers.emplace_back(
		     [&]
		     {
			     start.arrive_and_wait();
			     auto* component = cm.addComponent<RegistryComponent<Index>>(entity, static_cast<int>(Index));
			     bool valid = true;
			     for (std::size_t pass = 0; pass < 64; ++pass)
			     {
				     valid &= cm.hasComponent<Position>(entity);
				     valid &= cm.getComponent<RegistryComponent<Index>>(entity) == component;
			     }
			     validReads[Index] = valid;
		     }), ...);
	}(std::make_index_sequence<typeCount>{});
	for (std::thread& worker : workers) worker.join();

	for (bool valid : validReads) EXPECT_TRUE(valid);
	auto checkComponent = [&]<std::size_t Index>()
	{
		auto* component = cm.getComponent<RegistryComponent<Index>>(entity);
		ASSERT_NE(component, nullptr);
		EXPECT_EQ(component->value, static_cast<int>(Index));
	};
	[&]<std::size_t... Index>(std::index_sequence<Index...>)
	{
		(checkComponent.template operator()<Index>(), ...);
	}(std::make_index_sequence<typeCount>{});

	cm.removeEntity(entity);
	EXPECT_FALSE(cm.hasComponent<Position>(entity));
	auto checkRemoved = [&]<std::size_t Index>()
	{
		EXPECT_FALSE(cm.hasComponent<RegistryComponent<Index>>(entity));
	};
	[&]<std::size_t... Index>(std::index_sequence<Index...>)
	{
		(checkRemoved.template operator()<Index>(), ...);
	}(std::make_index_sequence<typeCount>{});
}

TEST(ComponentManager, RemovalCallbackCanRegisterStorage)
{
	ComponentManager cm;
	Entity removed{1, 0};
	Entity target{2, 0};
	cm.addComponent<RemovalCallbackComponent>(
	    removed, std::make_unique<RegisterComponentOnDestruction>(cm, target));

	cm.removeEntity(removed);

	EXPECT_FALSE(cm.hasComponent<RemovalCallbackComponent>(removed));
	auto* health = cm.getComponent<Health>(target);
	ASSERT_NE(health, nullptr);
	EXPECT_EQ(health->value, 42);

	cm.removeEntity(target);
	EXPECT_FALSE(cm.hasComponent<Health>(target));
}

struct RareSparseComponent
{
	static constexpr auto orhescyonStoragePolicy = StoragePolicy::Sparse;
	int payload;
};

struct ForeignSparseComponent
{
	int payload;
};
} // namespace

// Foreign types opt into Sparse via trait specialization instead of the in-type marker
template <>
struct Orhescyon::ComponentStorageTraits<ForeignSparseComponent>
{
	static constexpr Orhescyon::StoragePolicy policy = Orhescyon::StoragePolicy::Sparse;
	static constexpr uint32_t blockSize = 256;
};

namespace
{
TEST(ComponentManager, StoragePolicyDispatch)
{
	static_assert(std::is_same_v<ComponentManager::StorageFor<Position>, ComponentColumn<Position, 4096>>);
	static_assert(std::is_same_v<ComponentManager::StorageFor<RareSparseComponent>,
	                             SparseComponentStorage<RareSparseComponent, 4096>>);
	static_assert(std::is_same_v<ComponentManager::StorageFor<ForeignSparseComponent>,
	                             SparseComponentStorage<ForeignSparseComponent, 256>>);

	ComponentManager cm;
	Entity e1{1, 0};

	RareSparseComponent* rare = cm.addComponent<RareSparseComponent>(e1, 7);
	ASSERT_NE(rare, nullptr);
	EXPECT_EQ(rare->payload, 7);
	EXPECT_TRUE(cm.hasComponent<RareSparseComponent>(e1));

	cm.removeEntity(e1);
	EXPECT_FALSE(cm.hasComponent<RareSparseComponent>(e1));
}

} // namespace
