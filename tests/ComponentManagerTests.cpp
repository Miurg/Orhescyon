#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/Components/ComponentManager.hpp>
#include <gtest/gtest.h>

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
