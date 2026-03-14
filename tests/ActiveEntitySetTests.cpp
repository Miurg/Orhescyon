#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

TEST(ActiveEntitySet, InsertAndContains)
{
    ActiveEntitySet set;
    set.insert(1);
    set.insert(42);

    EXPECT_TRUE(set.contains(1));
    EXPECT_TRUE(set.contains(42));
    EXPECT_FALSE(set.contains(0));
    EXPECT_FALSE(set.contains(99));
}

TEST(ActiveEntitySet, EraseSwapsWithLast)
{
    ActiveEntitySet set;
    set.insert(1);
    set.insert(2);
    set.insert(3);

    set.erase(2);

    EXPECT_FALSE(set.contains(2));
    EXPECT_TRUE(set.contains(1));
    EXPECT_TRUE(set.contains(3));
    EXPECT_EQ(set.size(), 2u);
}

TEST(ActiveEntitySet, InsertDuplicateIsNoop)
{
    ActiveEntitySet set;
    set.insert(5);
    set.insert(5);

    EXPECT_EQ(set.size(), 1u);
}

TEST(ActiveEntitySet, EraseNonexistentIsNoop)
{
    ActiveEntitySet set;
    set.insert(1);
    set.erase(999);

    EXPECT_EQ(set.size(), 1u);
}

TEST(ActiveEntitySet, Clear)
{
    ActiveEntitySet set;
    set.insert(1);
    set.insert(2);
    set.clear();

    EXPECT_EQ(set.size(), 0u);
    EXPECT_FALSE(set.contains(1));
}
