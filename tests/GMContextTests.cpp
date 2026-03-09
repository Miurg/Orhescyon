#include <gtest/gtest.h>
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

struct Health { int value; };

struct GameConfig {};

TEST(GeneralManager, RegisterAndGetContext)
{
    GeneralManager gm;
    Entity cfg = gm.createEntity();
    gm.addComponent<Health>(cfg, 999);
    gm.registerContext<GameConfig>(cfg);

    Entity retrieved = gm.getContext<GameConfig>();
    EXPECT_EQ(cfg, retrieved);
}

TEST(GeneralManager, GetContextComponent)
{
    GeneralManager gm;
    Entity cfg = gm.createEntity();
    gm.addComponent<Health>(cfg, 42);
    gm.registerContext<GameConfig>(cfg);

    Health* h = gm.getContextComponent<GameConfig, Health>();
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->value, 42);
}