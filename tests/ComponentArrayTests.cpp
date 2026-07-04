#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

struct Position { float x, y; };

TEST(ComponentArray, AddAndGet)
{
    ComponentArray<Position> arr;
    arr.addComponent(Entity{1, 0}, Position{ 1.0f, 2.0f });

    Position* p = arr.getComponent(Entity{1, 0});
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 1.0f);
    EXPECT_FLOAT_EQ(p->y, 2.0f);
}

TEST(ComponentArray, GetNonexistentReturnsNull)
{
    ComponentArray<Position> arr;
    EXPECT_EQ(arr.getComponent(Entity{999, 0}), nullptr);

    for (uint32_t i = 0; i < 1000; i++)
    {
        arr.addComponent(Entity{i, 0}, Position{ 1.0f, 2.0f });
    }
    EXPECT_EQ(arr.getComponent(Entity{1001, 0}), nullptr);
}

TEST(ComponentArray, OverwriteExisting)
{
    ComponentArray<Position> arr;
    arr.addComponent(Entity{1, 0}, Position{ 1.0f, 2.0f });
    arr.addComponent(Entity{1, 0}, Position{ 9.0f, 9.0f });

    Position* p = arr.getComponent(Entity{1, 0});
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 9.0f);
}

TEST(ComponentArray, RemoveComponent)
{
    ComponentArray<Position> arr;
    arr.addComponent(Entity{1, 0}, Position{ 1.0f, 2.0f });
    arr.removeComponent(Entity{1, 0});

    EXPECT_EQ(arr.getComponent(Entity{1, 0}), nullptr);
    EXPECT_EQ(arr.size(), 0u);
}

TEST(ComponentArray, RemoveNonexistentIsNoop)
{
    ComponentArray<Position> arr;
    EXPECT_NO_THROW(arr.removeComponent(Entity{999, 0}));
}
