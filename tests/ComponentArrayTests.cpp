#include <gtest/gtest.h>
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

struct Position { float x, y; };

TEST(ComponentArray, AddAndGet)
{
    ComponentArray<Position> arr;
    arr.addComponent(1, Position{ 1.0f, 2.0f });

    Position* p = arr.getComponent(1);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 1.0f);
    EXPECT_FLOAT_EQ(p->y, 2.0f);
}

TEST(ComponentArray, GetNonexistentReturnsNull)
{
    ComponentArray<Position> arr;
    EXPECT_EQ(arr.getComponent(999), nullptr);
}

TEST(ComponentArray, OverwriteExisting)
{
    ComponentArray<Position> arr;
    arr.addComponent(1, Position{ 1.0f, 2.0f });
    arr.addComponent(1, Position{ 9.0f, 9.0f });

    Position* p = arr.getComponent(1);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 9.0f);
}

TEST(ComponentArray, RemoveComponent)
{
    ComponentArray<Position> arr;
    arr.addComponent(1, Position{ 1.0f, 2.0f });
    arr.removeComponent(1);

    EXPECT_EQ(arr.getComponent(1), nullptr);
    EXPECT_EQ(arr.size(), 0u);
}

TEST(ComponentArray, RemoveNonexistentIsNoop)
{
    ComponentArray<Position> arr;
    EXPECT_NO_THROW(arr.removeComponent(999));
}
