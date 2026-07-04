#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

TEST(ActiveEntitySet, InsertAndContains)
{
    ActiveEntitySet set;
    set.insert(Entity{1, 0});
    set.insert(Entity{42, 0});

    EXPECT_TRUE(set.contains(Entity{1, 0}));
    EXPECT_TRUE(set.contains(Entity{42, 0}));
    EXPECT_FALSE(set.contains(Entity{0, 0}));
    EXPECT_FALSE(set.contains(Entity{99, 0}));
}

TEST(ActiveEntitySet, GenerationMismatchIsNotContained)
{
    ActiveEntitySet set;
    set.insert(Entity{1, 0});

    EXPECT_FALSE(set.contains(Entity{1, 1}));
}

TEST(ActiveEntitySet, EraseStaleGenerationIsNoop)
{
    ActiveEntitySet set;
    set.insert(Entity{1, 0});
    set.erase(Entity{1, 1});

    EXPECT_TRUE(set.contains(Entity{1, 0}));
    EXPECT_EQ(set.size(), 1u);
}

TEST(ActiveEntitySet, EraseSwapsWithLast)
{
    ActiveEntitySet set;
    set.insert(Entity{1, 0});
    set.insert(Entity{2, 0});
    set.insert(Entity{3, 0});

    set.erase(Entity{2, 0});

    EXPECT_FALSE(set.contains(Entity{2, 0}));
    EXPECT_TRUE(set.contains(Entity{1, 0}));
    EXPECT_TRUE(set.contains(Entity{3, 0}));
    EXPECT_EQ(set.size(), 2u);
}

TEST(ActiveEntitySet, InsertDuplicateIsNoop)
{
    ActiveEntitySet set;
    set.insert(Entity{5, 0});
    set.insert(Entity{5, 0});

    EXPECT_EQ(set.size(), 1u);
}

TEST(ActiveEntitySet, EraseNonexistentIsNoop)
{
    ActiveEntitySet set;
    set.insert(Entity{1, 0});
    set.erase(Entity{999, 0});

    EXPECT_EQ(set.size(), 1u);
}

TEST(ActiveEntitySet, Clear)
{
    ActiveEntitySet set;
    set.insert(Entity{1, 0});
    set.insert(Entity{2, 0});
    set.clear();

    EXPECT_EQ(set.size(), 0u);
    EXPECT_FALSE(set.contains(Entity{1, 0}));
}
