#include <gtest/gtest.h>
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

namespace
{

struct Health { int value; };

struct GameConfig {};

struct SparseContextValue
{
    static constexpr auto orhescyonStoragePolicy = StoragePolicy::Sparse;
    int value;
};

TEST(GeneralManager, RegisterAndGetContext)
{
    GeneralManager gm;
    Entity cfg = gm.createEntity();
    gm.addComponentImmediate<Health>(cfg, 999);
    gm.registerContext<GameConfig>(cfg);

    Entity retrieved = gm.getContext<GameConfig>();
    EXPECT_EQ(cfg, retrieved);
}

TEST(GeneralManager, GetContextComponent)
{
    GeneralManager gm;
    Entity cfg = gm.createEntity();
    gm.addComponentImmediate<Health>(cfg, 42);
    gm.registerContext<GameConfig>(cfg);

    Health* h = gm.getContextComponent<GameConfig, Health>();
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->value, 42);
}

TEST(GeneralManager, InvalidContextRegistrationPreservesExistingBinding)
{
    GeneralManager gm(nullptr);
    const Entity current = gm.createEntity();
    const Entity stale = gm.createEntity();
    gm.destroyEntityImmediate(stale);
    const Entity recycled = gm.createEntity();
    ASSERT_EQ(stale.slot, recycled.slot);
    ASSERT_NE(stale.generation, recycled.generation);
    gm.registerContext<GameConfig>(current);

    gm.registerContext<GameConfig>(stale);
    EXPECT_EQ(gm.getContext<GameConfig>(), current);
    gm.registerContext<GameConfig>(Entity::invalid());
    EXPECT_EQ(gm.getContext<GameConfig>(), current);
}

TEST(GeneralManager, MissingAndStaleSparseContextComponentsReturnNull)
{
    GeneralManager gm(nullptr);
    const Entity context = gm.createEntity();
    gm.registerContext<GameConfig>(context);
    EXPECT_EQ((gm.getContextComponent<GameConfig, SparseContextValue>()), nullptr);

    SparseContextValue* value = gm.addComponentImmediate<SparseContextValue>(context, 7);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ((gm.getContextComponent<GameConfig, SparseContextValue>()), value);

    gm.destroyEntityImmediate(context);
    const Entity recycled = gm.createEntity();
    ASSERT_EQ(context.slot, recycled.slot);
    ASSERT_NE(context.generation, recycled.generation);
    SparseContextValue* replacement = gm.addComponentImmediate<SparseContextValue>(recycled, 42);
    ASSERT_NE(replacement, nullptr);

    EXPECT_EQ((gm.getContextComponent<GameConfig, SparseContextValue>()), nullptr);
    EXPECT_EQ(gm.getComponent<SparseContextValue>(recycled), replacement);
    EXPECT_EQ(replacement->value, 42);
}

} // namespace
