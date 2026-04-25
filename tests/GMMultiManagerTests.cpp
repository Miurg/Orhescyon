#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace Orhescyon;

struct MMPos
{
    float x = 0, y = 0;
};
struct MMVel
{
    float dx = 0, dy = 0;
};
struct MMHealth
{
    int value = 0;
};
struct MMCompA
{
    int v = 0;
};
struct MMCompB
{
    int v = 0;
};

static std::mutex g_mmLogMutex;
static std::vector<std::string> g_mmShutdownLog;

static void resetMMLog()
{
    std::lock_guard<std::mutex> lk(g_mmLogMutex);
    g_mmShutdownLog.clear();
}

static std::vector<std::string> snapshotMMLog()
{
    std::lock_guard<std::mutex> lk(g_mmLogMutex);
    return g_mmShutdownLog;
}

static void recordShutdown(std::string tag)
{
    std::lock_guard<std::mutex> lk(g_mmLogMutex);
    g_mmShutdownLog.push_back(std::move(tag));
}

TEST(MultiManager, DefaultSystemManagerAutoCreated)
{
    GeneralManager gm;
    EXPECT_NO_THROW(gm.getSystemManager("default"));
}

TEST(MultiManager, RegisterSystemManagerCreatesEmpty)
{
    GeneralManager gm;
    gm.registerSystemManager("physics");
    EXPECT_NO_THROW(gm.getSystemManager("physics"));
    EXPECT_NO_THROW(gm.update("physics"));
}

TEST(MultiManager, DuplicateRegisterSystemManagerIgnored)
{
    GeneralManager gm;
    gm.registerSystemManager("physics");
    EXPECT_NO_THROW(gm.registerSystemManager("physics"));
    EXPECT_NO_THROW(gm.getSystemManager("physics"));
}

TEST(MultiManager, GetUnknownSystemManagerThrows)
{
    GeneralManager gm;
    EXPECT_THROW(gm.getSystemManager("ghost"), std::runtime_error);
}

static std::atomic<int> g_physSystemCalls{0};
static std::atomic<int> g_defSystemCalls{0};

class PhysUpdate : public SystemCore<PhysUpdate>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager&) override { g_physSystemCalls.fetch_add(1); }
};

class DefUpdate : public SystemCore<DefUpdate>
{
public:
    void update(GeneralManager&) override { g_defSystemCalls.fetch_add(1); }
};

TEST(MultiManager, SystemRoutedByGetSystemManagerName)
{
    g_physSystemCalls = 0;
    g_defSystemCalls = 0;

    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<PhysUpdate>();
    gm.registerSystem<DefUpdate>();

    gm.update("physics");
    EXPECT_EQ(g_physSystemCalls.load(), 1);
    EXPECT_EQ(g_defSystemCalls.load(), 0);

    gm.update("default");
    EXPECT_EQ(g_physSystemCalls.load(), 1);
    EXPECT_EQ(g_defSystemCalls.load(), 1);
}

TEST(MultiManager, SystemWithoutOverrideGoesToDefault)
{
    g_defSystemCalls = 0;

    GeneralManager gm;
    gm.registerSystem<DefUpdate>();

    gm.update();
    EXPECT_EQ(g_defSystemCalls.load(), 1);
}

static std::atomic<int> g_unknownRegisteredCalls{0};

class UnknownManagerSystem : public SystemCore<UnknownManagerSystem>
{
public:
    std::string_view getSystemManagerName() const override { return "nonexistent"; }
    void onRegistered(GeneralManager&) override { g_unknownRegisteredCalls.fetch_add(1); }
    void update(GeneralManager&) override {}
};

TEST(MultiManager, RegisterSystemIntoUnknownManagerWarnsAndSkips)
{
    g_unknownRegisteredCalls = 0;

    GeneralManager gm;
    EXPECT_NO_THROW(gm.registerSystem<UnknownManagerSystem>());
    EXPECT_EQ(g_unknownRegisteredCalls.load(), 0);
}

class PhysCount : public SystemCore<PhysCount>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager&) override { g_physSystemCalls.fetch_add(1); }
};

class RenderCount : public SystemCore<RenderCount>
{
public:
    std::string_view getSystemManagerName() const override { return "render"; }
    void update(GeneralManager&) override {}
};

TEST(MultiManager, UpdateByNameRunsOnlyThatManager)
{
    g_physSystemCalls = 0;
    g_defSystemCalls = 0;

    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<PhysCount>();
    gm.registerSystem<DefUpdate>();

    gm.update("physics");
    EXPECT_EQ(g_physSystemCalls.load(), 1);
    EXPECT_EQ(g_defSystemCalls.load(), 0);
}

TEST(MultiManager, UpdateShortcutDispatchesToDefault)
{
    g_defSystemCalls = 0;

    GeneralManager gm;
    gm.registerSystem<DefUpdate>();

    gm.update();
    gm.update("default");
    EXPECT_EQ(g_defSystemCalls.load(), 2);
}

TEST(MultiManager, UpdateUnknownThrows)
{
    GeneralManager gm;
    EXPECT_THROW(gm.update("ghost"), std::runtime_error);
}

TEST(MultiManager, UpdateEmptyManagerIsNoop)
{
    GeneralManager gm;
    gm.registerSystemManager("empty");
    EXPECT_NO_THROW(gm.update("empty"));
}

static std::atomic<int> g_physSubscribed{0};
static std::atomic<int> g_physUnsubscribed{0};
static std::atomic<int> g_defSubscribed{0};
static std::atomic<int> g_defUnsubscribed{0};

class PhysMove : public SystemCore<PhysMove, MMPos, MMVel>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager&) override {}
    void onEntitySubscribed(Entity, GeneralManager&) override { g_physSubscribed.fetch_add(1); }
    void onEntityUnsubscribed(Entity, GeneralManager&) override { g_physUnsubscribed.fetch_add(1); }
};

class DefHealth : public SystemCore<DefHealth, MMHealth>
{
public:
    void update(GeneralManager&) override {}
    void onEntitySubscribed(Entity, GeneralManager&) override { g_defSubscribed.fetch_add(1); }
    void onEntityUnsubscribed(Entity, GeneralManager&) override { g_defUnsubscribed.fetch_add(1); }
};

TEST(MultiManager, SubscribeRoutedByPriorRegistration)
{
    g_physSubscribed = 0;
    g_defSubscribed = 0;

    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<PhysMove>();
    gm.registerSystem<DefHealth>();

    Entity e = gm.createEntity();
    gm.addComponent<MMPos>(e);
    gm.addComponent<MMVel>(e);
    gm.subscribeEntity<PhysMove>(e);

    EXPECT_EQ(g_physSubscribed.load(), 1);
    EXPECT_EQ(g_defSubscribed.load(), 0);
}

TEST(MultiManager, UnsubscribeRoutedByPriorRegistration)
{
    g_physSubscribed = 0;
    g_physUnsubscribed = 0;

    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<PhysMove>();

    Entity e = gm.createEntity();
    gm.addComponent<MMPos>(e);
    gm.addComponent<MMVel>(e);
    gm.subscribeEntity<PhysMove>(e);
    gm.unsubscribeEntity<PhysMove>(e);

    EXPECT_EQ(g_physSubscribed.load(), 1);
    EXPECT_EQ(g_physUnsubscribed.load(), 1);
}

TEST(MultiManager, SubscribeBeforeRegisterWarnsNoCrash)
{
    GeneralManager gm;
    Entity e = gm.createEntity();
    gm.addComponent<MMPos>(e);
    gm.addComponent<MMVel>(e);

    EXPECT_NO_THROW(gm.subscribeEntity<PhysMove>(e));
}

TEST(MultiManager, DestroyEntityUnsubscribesFromAllManagers)
{
    g_physSubscribed = 0;
    g_physUnsubscribed = 0;
    g_defSubscribed = 0;
    g_defUnsubscribed = 0;

    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<PhysMove>();
    gm.registerSystem<DefHealth>();

    Entity e = gm.createEntity();
    gm.addComponent<MMPos>(e);
    gm.addComponent<MMVel>(e);
    gm.addComponent<MMHealth>(e);

    gm.subscribeEntity<PhysMove>(e);
    gm.subscribeEntity<DefHealth>(e);
    EXPECT_EQ(g_physSubscribed.load(), 1);
    EXPECT_EQ(g_defSubscribed.load(), 1);

    gm.destroyEntity(e);
    EXPECT_EQ(g_physUnsubscribed.load(), 1);
    EXPECT_EQ(g_defUnsubscribed.load(), 1);
}

TEST(MultiManager, DestroyEntityNotSubscribedAnywhereIsSafe)
{
    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<PhysMove>();
    gm.registerSystem<DefHealth>();

    Entity e = gm.createEntity();
    EXPECT_NO_THROW(gm.destroyEntity(e));
}

TEST(MultiManager, RemoveComponentAutoUnsubscribesCorrectManager)
{
    g_physSubscribed = 0;
    g_physUnsubscribed = 0;
    g_defSubscribed = 0;
    g_defUnsubscribed = 0;

    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<PhysMove>();
    gm.registerSystem<DefHealth>();

    Entity e = gm.createEntity();
    gm.addComponent<MMPos>(e);
    gm.addComponent<MMVel>(e);
    gm.addComponent<MMHealth>(e);
    gm.subscribeEntity<PhysMove>(e);
    gm.subscribeEntity<DefHealth>(e);

    gm.removeComponent<MMPos>(e);

    // checkEntitySubscriptions removes the link without firing onEntityUnsubscribed;
    // verify indirectly via resubscribe (HIGH_CHECK would warn "already subscribed").
    EXPECT_EQ(g_physUnsubscribed.load(), 0);
    gm.addComponent<MMPos>(e);
    EXPECT_NO_THROW(gm.subscribeEntity<PhysMove>(e));
    EXPECT_EQ(g_physSubscribed.load(), 2);

    EXPECT_EQ(g_defUnsubscribed.load(), 0);
    EXPECT_EQ(g_defSubscribed.load(), 1);
}

class PhysDagA : public SystemCore<PhysDagA>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager&) override {}
    std::vector<std::type_index> getWriteComponents() override { return {typeid(MMCompA)}; }
};
class PhysDagB : public SystemCore<PhysDagB>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager&) override {}
    std::vector<std::type_index> getReadComponents() override { return {typeid(MMCompA)}; }
};

// Inverse dep direction on same components — would cycle if combined into one SM.
class RendDagA : public SystemCore<RendDagA>
{
public:
    std::string_view getSystemManagerName() const override { return "render"; }
    void update(GeneralManager&) override {}
    std::vector<std::type_index> getReadComponents() override { return {typeid(MMCompA)}; }
};
class RendDagB : public SystemCore<RendDagB>
{
public:
    std::string_view getSystemManagerName() const override { return "render"; }
    void update(GeneralManager&) override {}
    std::vector<std::type_index> getWriteComponents() override { return {typeid(MMCompA)}; }
};

TEST(MultiManager, ManagerDagsAreIndependent)
{
    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystemManager("render");

    gm.registerSystem<PhysDagA>();
    gm.registerSystem<PhysDagB>();
    gm.registerSystem<RendDagA>();
    gm.registerSystem<RendDagB>();

    EXPECT_NO_THROW(gm.update("physics"));
    EXPECT_NO_THROW(gm.update("render"));
}

class PhysSleepA : public SystemCore<PhysSleepA>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager&) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};
class PhysSleepB : public SystemCore<PhysSleepB>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager&) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

TEST(MultiManager, ParallelismIsPerManager)
{
    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<PhysSleepA>();
    gm.registerSystem<PhysSleepB>();

    auto start = std::chrono::steady_clock::now();
    gm.update("physics");
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Sequential ~100ms, parallel ~50ms; 90ms leaves margin for scheduling jitter.
    EXPECT_LT(ms, 90);
}

class PhysCycP : public SystemCore<PhysCycP>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager&) override {}
    std::vector<std::type_index> getBeforeSystems() override;
};
class PhysCycQ : public SystemCore<PhysCycQ>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager&) override {}
    std::vector<std::type_index> getBeforeSystems() override;
};
std::vector<std::type_index> PhysCycP::getBeforeSystems() { return {typeid(PhysCycQ)}; }
std::vector<std::type_index> PhysCycQ::getBeforeSystems() { return {typeid(PhysCycP)}; }

TEST(MultiManager, CycleWithinSingleManagerStillThrows)
{
    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<PhysCycP>();
    gm.registerSystem<PhysCycQ>();

    EXPECT_THROW(gm.update("physics"), std::runtime_error);
}

class ShutdownRecorderA : public SystemCore<ShutdownRecorderA>
{
public:
    std::string_view getSystemManagerName() const override { return "a"; }
    void update(GeneralManager&) override {}
    void onShutdown(GeneralManager&) override { recordShutdown("a"); }
};
class ShutdownRecorderB : public SystemCore<ShutdownRecorderB>
{
public:
    std::string_view getSystemManagerName() const override { return "b"; }
    void update(GeneralManager&) override {}
    void onShutdown(GeneralManager&) override { recordShutdown("b"); }
};
class ShutdownRecorderC : public SystemCore<ShutdownRecorderC>
{
public:
    std::string_view getSystemManagerName() const override { return "c"; }
    void update(GeneralManager&) override {}
    void onShutdown(GeneralManager&) override { recordShutdown("c"); }
};
class ShutdownRecorderDefault : public SystemCore<ShutdownRecorderDefault>
{
public:
    void update(GeneralManager&) override {}
    void onShutdown(GeneralManager&) override { recordShutdown("default"); }
};

TEST(MultiManager, ShutdownOrderIsReverseOfRegistration)
{
    resetMMLog();
    {
        GeneralManager gm;
        gm.registerSystemManager("a");
        gm.registerSystemManager("b");
        gm.registerSystemManager("c");

        gm.registerSystem<ShutdownRecorderDefault>();
        gm.registerSystem<ShutdownRecorderA>();
        gm.registerSystem<ShutdownRecorderB>();
        gm.registerSystem<ShutdownRecorderC>();
    }

    auto log = snapshotMMLog();
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[0], "c");
    EXPECT_EQ(log[1], "b");
    EXPECT_EQ(log[2], "a");
    EXPECT_EQ(log[3], "default");
}

class S1InDefault : public SystemCore<S1InDefault>
{
public:
    void update(GeneralManager&) override {}
    void onShutdown(GeneralManager&) override { recordShutdown("S1"); }
};
class S2InDefault : public SystemCore<S2InDefault>
{
public:
    void update(GeneralManager&) override {}
    void onShutdown(GeneralManager&) override { recordShutdown("S2"); }
};
class S3InDefault : public SystemCore<S3InDefault>
{
public:
    void update(GeneralManager&) override {}
    void onShutdown(GeneralManager&) override { recordShutdown("S3"); }
};

TEST(MultiManager, ShutdownWithinManagerKeepsReverseSystemOrder)
{
    resetMMLog();
    {
        GeneralManager gm;
        gm.registerSystem<S1InDefault>();
        gm.registerSystem<S2InDefault>();
        gm.registerSystem<S3InDefault>();
    }

    auto log = snapshotMMLog();
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "S3");
    EXPECT_EQ(log[1], "S2");
    EXPECT_EQ(log[2], "S1");
}

static std::atomic<bool> g_selfLookupOk{false};

class SelfLookup : public SystemCore<SelfLookup>
{
public:
    std::string_view getSystemManagerName() const override { return "physics"; }
    void update(GeneralManager& gm) override
    {
        SystemManager& mine = gm.getSystemManager(getSystemManagerName());
        (void)mine;
        g_selfLookupOk.store(true);
    }
};

TEST(MultiManager, SystemReachesOwnManager)
{
    g_selfLookupOk = false;

    GeneralManager gm;
    gm.registerSystemManager("physics");
    gm.registerSystem<SelfLookup>();
    gm.update("physics");

    EXPECT_TRUE(g_selfLookupOk.load());
}

static std::atomic<int> g_dualInstanceUpdates{0};
static std::atomic<int> g_dualInstanceSubscribed{0};

class DualInstance : public SystemCore<DualInstance>
{
public:
    std::string_view getSystemManagerName() const override { return _name; }
    void setName(std::string_view n) { _name = n; }
    void update(GeneralManager&) override { g_dualInstanceUpdates.fetch_add(1); }
    void onEntitySubscribed(Entity, GeneralManager&) override { g_dualInstanceSubscribed.fetch_add(1); }

private:
    std::string_view _name = "default";
};

// getSystemManagerName() is read from a freshly-constructed instance, so the
// public API can't route the same type into two named managers; this test
// pins down the in-manager idempotency case instead.
TEST(MultiManager, SameTypeRegisteredTwiceInSameManager)
{
    g_dualInstanceUpdates = 0;

    GeneralManager gm;
    gm.registerSystem<DualInstance>();
    gm.registerSystem<DualInstance>();

    gm.update();
    EXPECT_EQ(g_dualInstanceUpdates.load(), 2);
}
