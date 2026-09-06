#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

using namespace Orhescyon;

namespace
{

struct Health { int value; };

struct GameConfig {};

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

} // namespace
