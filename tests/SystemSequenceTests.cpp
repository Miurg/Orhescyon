#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using namespace Orhescyon;

namespace
{

// Component stubs — used only as type tokens for dependency declarations
struct CompA
{
    int val = 0;
};
struct CompB
{
    int val = 0;
};
struct CompC
{
    int val = 0;
};

static std::mutex g_logMutex;
static std::vector<int> g_order;

static void resetLog()
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    g_order.clear();
}

static void record(int id)
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    g_order.push_back(id);
}

// Returns position of `id` in g_order, or -1 if absent.
static int pos(int id)
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    for (int i = 0; i < static_cast<int>(g_order.size()); ++i)
        if (g_order[i] == id) return i;
    return -1;
}

// Systems record their id in update(); all scheduling is declared at registration.
class IndA : public SystemCore<IndA>
{
public:
    void update(GeneralManager&) override { record(0); }
};
class IndB : public SystemCore<IndB>
{
public:
    void update(GeneralManager&) override { record(1); }
};
class IndC : public SystemCore<IndC>
{
public:
    void update(GeneralManager&) override { record(2); }
};

class ChainA : public SystemCore<ChainA>
{
public:
    void update(GeneralManager&) override { record(10); }
};
class ChainB : public SystemCore<ChainB>
{
public:
    void update(GeneralManager&) override { record(11); }
};
class ChainC : public SystemCore<ChainC>
{
public:
    void update(GeneralManager&) override { record(12); }
};

class DiamondA : public SystemCore<DiamondA>
{
public:
    void update(GeneralManager&) override { record(20); }
};
class DiamondB : public SystemCore<DiamondB>
{
public:
    void update(GeneralManager&) override { record(21); }
};
class DiamondC : public SystemCore<DiamondC>
{
public:
    void update(GeneralManager&) override { record(22); }
};
class DiamondD : public SystemCore<DiamondD>
{
public:
    void update(GeneralManager&) override { record(23); }
};

class SysWriter : public SystemCore<SysWriter>
{
public:
    void update(GeneralManager&) override { record(30); }
};
class SysReader : public SystemCore<SysReader>
{
public:
    void update(GeneralManager&) override { record(31); }
};

class SysWriterA : public SystemCore<SysWriterA>
{
public:
    void update(GeneralManager&) override { record(40); }
};
class SysWriterB : public SystemCore<SysWriterB>
{
public:
    void update(GeneralManager&) override { record(41); }
};

class ExplBeforeY : public SystemCore<ExplBeforeY>
{
public:
    void update(GeneralManager&) override { record(51); }
};
class ExplBeforeX : public SystemCore<ExplBeforeX>
{
public:
    void update(GeneralManager&) override { record(50); }
};

class ExplAfterX : public SystemCore<ExplAfterX>
{
public:
    void update(GeneralManager&) override { record(60); }
};
class ExplAfterY : public SystemCore<ExplAfterY>
{
public:
    void update(GeneralManager&) override { record(61); }
};

class MixedC : public SystemCore<MixedC>
{
public:
    void update(GeneralManager&) override { record(72); }
};
class MixedA : public SystemCore<MixedA>
{
public:
    void update(GeneralManager&) override { record(70); }
};
class MixedB : public SystemCore<MixedB>
{
public:
    void update(GeneralManager&) override { record(71); }
};

class CycP : public SystemCore<CycP>
{
public:
    void update(GeneralManager&) override {}
};
class CycQ : public SystemCore<CycQ>
{
public:
    void update(GeneralManager&) override {}
};

class CycDataA : public SystemCore<CycDataA>
{
public:
    void update(GeneralManager&) override {}
};
class CycDataB : public SystemCore<CycDataB>
{
public:
    void update(GeneralManager&) override {}
};

class CycThreeA : public SystemCore<CycThreeA>
{
public:
    void update(GeneralManager&) override {}
};
class CycThreeB : public SystemCore<CycThreeB>
{
public:
    void update(GeneralManager&) override {}
};
class CycThreeC : public SystemCore<CycThreeC>
{
public:
    void update(GeneralManager&) override {}
};

// Referenced as a before-target but never registered
class SysNeverRegistered : public SystemCore<SysNeverRegistered>
{
public:
    void update(GeneralManager&) override {}
};
class SysBeforeGhost : public SystemCore<SysBeforeGhost>
{
public:
    void update(GeneralManager&) override { record(90); }
};

class SysSleepA : public SystemCore<SysSleepA>
{
public:
    void update(GeneralManager&) override
    {
        record(100);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};
class SysSleepB : public SystemCore<SysSleepB>
{
public:
    void update(GeneralManager&) override
    {
        record(101);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

static std::atomic<int> g_sharedValue{0};
static std::atomic<int> g_observedValue{0};

class DepWriter : public SystemCore<DepWriter>
{
public:
    void update(GeneralManager&) override
    {
        g_sharedValue.store(42, std::memory_order_release);
        record(110);
    }
};
class DepReader : public SystemCore<DepReader>
{
public:
    void update(GeneralManager&) override
    {
        g_observedValue.store(g_sharedValue.load(std::memory_order_acquire));
        record(111);
    }
};

class SysThreadCheck : public SystemCore<SysThreadCheck>
{
public:
    static inline std::thread::id recordedId;
    void update(GeneralManager&) override { recordedId = std::this_thread::get_id(); }
};

template <int Id>
class BulkSys : public SystemCore<BulkSys<Id>>
{
public:
    static inline std::atomic<int> counter{0};
    void update(GeneralManager&) override { ++counter; }
};

class CacheA : public SystemCore<CacheA>
{
public:
    static inline std::atomic<int> counter{0};
    void update(GeneralManager&) override { ++counter; }
};
class CacheB : public SystemCore<CacheB>
{
public:
    static inline std::atomic<int> counter{0};
    void update(GeneralManager&) override { ++counter; }
};

TEST(SystemSequence, IndependentSystemsSameLayer)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<IndA>();
    gm.registerSystem<IndB>();
    gm.registerSystem<IndC>();
    gm.update();

    EXPECT_NE(pos(0), -1);
    EXPECT_NE(pos(1), -1);
    EXPECT_NE(pos(2), -1);
}

TEST(SystemSequence, LinearChainOrdering)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<ChainA>().writes<CompA>();
    gm.registerSystem<ChainB>().reads<CompA>().writes<CompB>();
    gm.registerSystem<ChainC>().reads<CompB>();
    gm.update();

    ASSERT_NE(pos(10), -1);
    ASSERT_NE(pos(11), -1);
    ASSERT_NE(pos(12), -1);
    EXPECT_LT(pos(10), pos(11));
    EXPECT_LT(pos(11), pos(12));
}

TEST(SystemSequence, DiamondPatternOrdering)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<DiamondA>().writes<CompA>();
    gm.registerSystem<DiamondB>().reads<CompA>().writes<CompB>();
    gm.registerSystem<DiamondC>().reads<CompA>().writes<CompC>();
    gm.registerSystem<DiamondD>().reads<CompB, CompC>();
    gm.update();

    ASSERT_NE(pos(20), -1);
    ASSERT_NE(pos(21), -1);
    ASSERT_NE(pos(22), -1);
    ASSERT_NE(pos(23), -1);
    EXPECT_LT(pos(20), pos(21)); // A before B
    EXPECT_LT(pos(20), pos(22)); // A before C
    EXPECT_LT(pos(21), pos(23)); // B before D
    EXPECT_LT(pos(22), pos(23)); // C before D
}

TEST(SystemSequence, WriteReadConflict)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<SysWriter>().writes<CompA>();
    gm.registerSystem<SysReader>().reads<CompA>();
    gm.update();

    ASSERT_NE(pos(30), -1);
    ASSERT_NE(pos(31), -1);
    EXPECT_LT(pos(30), pos(31));
}

TEST(SystemSequence, WriteWriteConflict)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<SysWriterA>().writes<CompA>();
    gm.registerSystem<SysWriterB>().writes<CompA>();
    gm.update();

    ASSERT_NE(pos(40), -1);
    ASSERT_NE(pos(41), -1);
    EXPECT_LT(pos(40), pos(41));
}

TEST(SystemSequence, ExplicitBefore)
{
    resetLog();
    GeneralManager gm;
    // Forward reference: the target registers after the declaration
    gm.registerSystem<ExplBeforeX>().before<ExplBeforeY>();
    gm.registerSystem<ExplBeforeY>();
    gm.update();

    ASSERT_NE(pos(50), -1);
    ASSERT_NE(pos(51), -1);
    EXPECT_LT(pos(50), pos(51));
}

TEST(SystemSequence, ExplicitAfter)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<ExplAfterX>();
    gm.registerSystem<ExplAfterY>().after<ExplAfterX>();
    gm.update();

    ASSERT_NE(pos(60), -1);
    ASSERT_NE(pos(61), -1);
    EXPECT_LT(pos(60), pos(61));
}

TEST(SystemSequence, MixedDependencies)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<MixedA>().writes<CompA>();
    gm.registerSystem<MixedB>().reads<CompA>().before<MixedC>();
    gm.registerSystem<MixedC>();
    gm.update();

    ASSERT_NE(pos(70), -1);
    ASSERT_NE(pos(71), -1);
    ASSERT_NE(pos(72), -1);
    EXPECT_LT(pos(70), pos(71)); // A before B (data dep: write/read CompA)
    EXPECT_LT(pos(71), pos(72)); // B before C (explicit before)
}

TEST(SystemSequence, CircularExplicitDependencyThrows)
{
    GeneralManager gm;
    gm.registerSystem<CycP>().before<CycQ>();
    gm.registerSystem<CycQ>().before<CycP>();
    EXPECT_THROW(gm.update(), std::runtime_error);
}

TEST(SystemSequence, CircularDataDependencyThrows)
{
    GeneralManager gm;
    gm.registerSystem<CycDataA>().writes<CompA>().reads<CompB>();
    gm.registerSystem<CycDataB>().writes<CompB>().reads<CompA>();
    EXPECT_THROW(gm.update(), std::runtime_error);
}

TEST(SystemSequence, CircularThreeSystemsThrows)
{
    GeneralManager gm;
    gm.registerSystem<CycThreeA>().before<CycThreeB>();
    gm.registerSystem<CycThreeB>().before<CycThreeC>();
    gm.registerSystem<CycThreeC>().before<CycThreeA>();
    EXPECT_THROW(gm.update(), std::runtime_error);
}

TEST(SystemSequence, BeforeNonExistentSystemNoThrow)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<SysBeforeGhost>().before<SysNeverRegistered>();
    EXPECT_NO_THROW(gm.update());
    EXPECT_NE(pos(90), -1);
}

TEST(SystemSequence, IndependentSystemsAllExecute)
{
    BulkSys<0>::counter = 0;
    BulkSys<1>::counter = 0;
    BulkSys<2>::counter = 0;
    BulkSys<3>::counter = 0;

    GeneralManager gm;
    gm.registerSystem<BulkSys<0>>();
    gm.registerSystem<BulkSys<1>>();
    gm.registerSystem<BulkSys<2>>();
    gm.registerSystem<BulkSys<3>>();
    gm.update();

    EXPECT_EQ(BulkSys<0>::counter.load(), 1);
    EXPECT_EQ(BulkSys<1>::counter.load(), 1);
    EXPECT_EQ(BulkSys<2>::counter.load(), 1);
    EXPECT_EQ(BulkSys<3>::counter.load(), 1);
}

TEST(SystemSequence, IndependentSystemsRunConcurrently)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<SysSleepA>();
    gm.registerSystem<SysSleepB>();

    auto start = std::chrono::steady_clock::now();
    gm.update();
    auto elapsed = std::chrono::steady_clock::now() - start;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    // Sequential would take ~100ms, parallel ~50ms. Use generous margin.
    EXPECT_LT(ms, 90);
    EXPECT_NE(pos(100), -1);
    EXPECT_NE(pos(101), -1);
}

TEST(SystemSequence, DependentSystemsCorrectOrder)
{
    resetLog();
    g_sharedValue = 0;
    g_observedValue = 0;

    GeneralManager gm;
    gm.registerSystem<DepWriter>().writes<CompA>();
    gm.registerSystem<DepReader>().reads<CompA>();
    gm.update();

    EXPECT_EQ(g_observedValue.load(), 42);
    EXPECT_LT(pos(110), pos(111));
}

TEST(SystemSequence, SingleSystemRunsOnCallerThread)
{
    SysThreadCheck::recordedId = std::thread::id{};

    GeneralManager gm;
    gm.registerSystem<SysThreadCheck>();

    auto callerThread = std::this_thread::get_id();
    gm.update();

    EXPECT_EQ(SysThreadCheck::recordedId, callerThread);
}

TEST(SystemSequence, NoSystemsUpdateSafe)
{
    GeneralManager gm;
    EXPECT_NO_THROW(gm.update());
}

TEST(SystemSequence, SingleSystemRuns)
{
    BulkSys<5>::counter = 0;

    GeneralManager gm;
    gm.registerSystem<BulkSys<5>>();
    gm.update();
    gm.update();

    EXPECT_EQ(BulkSys<5>::counter.load(), 2);
}

TEST(SystemSequence, ManyIndependentSystems)
{
    BulkSys<10>::counter = 0;
    BulkSys<11>::counter = 0;
    BulkSys<12>::counter = 0;
    BulkSys<13>::counter = 0;
    BulkSys<14>::counter = 0;
    BulkSys<15>::counter = 0;
    BulkSys<16>::counter = 0;
    BulkSys<17>::counter = 0;
    BulkSys<18>::counter = 0;
    BulkSys<19>::counter = 0;

    GeneralManager gm;
    gm.registerSystem<BulkSys<10>>();
    gm.registerSystem<BulkSys<11>>();
    gm.registerSystem<BulkSys<12>>();
    gm.registerSystem<BulkSys<13>>();
    gm.registerSystem<BulkSys<14>>();
    gm.registerSystem<BulkSys<15>>();
    gm.registerSystem<BulkSys<16>>();
    gm.registerSystem<BulkSys<17>>();
    gm.registerSystem<BulkSys<18>>();
    gm.registerSystem<BulkSys<19>>();
    gm.update();

    EXPECT_EQ(BulkSys<10>::counter.load(), 1);
    EXPECT_EQ(BulkSys<11>::counter.load(), 1);
    EXPECT_EQ(BulkSys<12>::counter.load(), 1);
    EXPECT_EQ(BulkSys<13>::counter.load(), 1);
    EXPECT_EQ(BulkSys<14>::counter.load(), 1);
    EXPECT_EQ(BulkSys<15>::counter.load(), 1);
    EXPECT_EQ(BulkSys<16>::counter.load(), 1);
    EXPECT_EQ(BulkSys<17>::counter.load(), 1);
    EXPECT_EQ(BulkSys<18>::counter.load(), 1);
    EXPECT_EQ(BulkSys<19>::counter.load(), 1);
}

TEST(SystemSequence, AddSystemInvalidatesCache)
{
    CacheA::counter = 0;
    CacheB::counter = 0;

    GeneralManager gm;
    gm.registerSystem<CacheA>();
    gm.update();
    EXPECT_EQ(CacheA::counter.load(), 1);

    gm.registerSystem<CacheB>();
    gm.update();
    EXPECT_EQ(CacheA::counter.load(), 2);
    EXPECT_EQ(CacheB::counter.load(), 1);
}

TEST(SystemSequence, MultipleUpdatesStable)
{
    BulkSys<20>::counter = 0;
    BulkSys<21>::counter = 0;

    GeneralManager gm;
    gm.registerSystem<BulkSys<20>>();
    gm.registerSystem<BulkSys<21>>();
    gm.update();
    gm.update();
    gm.update();

    EXPECT_EQ(BulkSys<20>::counter.load(), 3);
    EXPECT_EQ(BulkSys<21>::counter.load(), 3);
}

// Write-write pair split in time by an explicit barrier must not produce a cycle,
// regardless of registration order.
class WWBarrier : public SystemCore<WWBarrier>
{
public:
    void update(GeneralManager&) override { record(200); }
};
class WWBeforeBarrier : public SystemCore<WWBeforeBarrier>
{
public:
    void update(GeneralManager&) override { record(201); }
};
class WWAfterBarrier : public SystemCore<WWAfterBarrier>
{
public:
    void update(GeneralManager&) override { record(202); }
};

TEST(SystemSequence, WriteWriteWithBarrierRegisteredBeforeAfter)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<WWBarrier>();
    gm.registerSystem<WWBeforeBarrier>().writes<CompA>().before<WWBarrier>();
    gm.registerSystem<WWAfterBarrier>().writes<CompA>().after<WWBarrier>();
    EXPECT_NO_THROW(gm.update());

    ASSERT_NE(pos(200), -1);
    ASSERT_NE(pos(201), -1);
    ASSERT_NE(pos(202), -1);
    EXPECT_LT(pos(201), pos(200));
    EXPECT_LT(pos(200), pos(202));
}

TEST(SystemSequence, WriteWriteWithBarrierRegisteredAfterBefore)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<WWBarrier>();
    gm.registerSystem<WWAfterBarrier>().writes<CompA>().after<WWBarrier>();
    gm.registerSystem<WWBeforeBarrier>().writes<CompA>().before<WWBarrier>();
    EXPECT_NO_THROW(gm.update());

    ASSERT_NE(pos(200), -1);
    ASSERT_NE(pos(201), -1);
    ASSERT_NE(pos(202), -1);
    EXPECT_LT(pos(201), pos(200));
    EXPECT_LT(pos(200), pos(202));
}

// Partial write-write conflict: Y and Z do not conflict with each other and should
// stay parallel while X runs alone.
class WWPartialX : public SystemCore<WWPartialX>
{
public:
    void update(GeneralManager&) override
    {
        record(210);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};
class WWPartialY : public SystemCore<WWPartialY>
{
public:
    void update(GeneralManager&) override
    {
        record(211);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};
class WWPartialZ : public SystemCore<WWPartialZ>
{
public:
    void update(GeneralManager&) override
    {
        record(212);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

TEST(SystemSequence, WriteWritePartialConflictKeepsParallelism)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<WWPartialX>().writes<CompA, CompB>();
    gm.registerSystem<WWPartialY>().writes<CompA>();
    gm.registerSystem<WWPartialZ>().writes<CompB>();

    auto start = std::chrono::steady_clock::now();
    gm.update();
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // X alone (~50ms) then Y and Z parallel (~50ms) should total ~100ms.
    // Full serialization would be ~150ms.
    EXPECT_LT(ms, 140);
    ASSERT_NE(pos(210), -1);
    ASSERT_NE(pos(211), -1);
    ASSERT_NE(pos(212), -1);
    EXPECT_LT(pos(210), pos(211));
    EXPECT_LT(pos(210), pos(212));
}

// Write-write pair must be serialized in time, not just ordered in the log.
class WWSerialA : public SystemCore<WWSerialA>
{
public:
    void update(GeneralManager&) override
    {
        record(220);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};
class WWSerialB : public SystemCore<WWSerialB>
{
public:
    void update(GeneralManager&) override
    {
        record(221);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

TEST(SystemSequence, WriteWriteActuallySerialized)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<WWSerialA>().writes<CompA>();
    gm.registerSystem<WWSerialB>().writes<CompA>();

    auto start = std::chrono::steady_clock::now();
    gm.update();
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Sequential ~100ms. Parallel (regression) ~50ms. Generous lower bound.
    EXPECT_GE(ms, 95);
    EXPECT_LT(pos(220), pos(221));
}

// 4 non-writers + 2 write-writers in one layer: the split should keep most parallelism.
template <int Id>
class WWParaNoWrite : public SystemCore<WWParaNoWrite<Id>>
{
public:
    void update(GeneralManager&) override { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
};
class WWParaWriter1 : public SystemCore<WWParaWriter1>
{
public:
    void update(GeneralManager&) override { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
};
class WWParaWriter2 : public SystemCore<WWParaWriter2>
{
public:
    void update(GeneralManager&) override { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
};

TEST(SystemSequence, WriteWriteParallelismPreservedAroundConflict)
{
    GeneralManager gm;
    gm.registerSystem<WWParaNoWrite<0>>();
    gm.registerSystem<WWParaNoWrite<1>>();
    gm.registerSystem<WWParaNoWrite<2>>();
    gm.registerSystem<WWParaNoWrite<3>>();
    gm.registerSystem<WWParaWriter1>().writes<CompA>();
    gm.registerSystem<WWParaWriter2>().writes<CompA>();

    auto start = std::chrono::steady_clock::now();
    gm.update();
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Correct split: 5-wide sublayer (~50-150ms depending on thread count) + 1 alone (~50ms).
    // Regression where the conflict drags everyone into serial: 6 * 50ms = 300ms.
    EXPECT_LT(ms, 250);
}

// Two stacked layers, each with its own write-write pair — the split must be
// computed per layer, without leaking write sets across layers.
class WWStackL1A : public SystemCore<WWStackL1A>
{
public:
    void update(GeneralManager&) override { record(240); }
};
class WWStackL1B : public SystemCore<WWStackL1B>
{
public:
    void update(GeneralManager&) override { record(241); }
};
class WWStackL2C : public SystemCore<WWStackL2C>
{
public:
    void update(GeneralManager&) override { record(242); }
};
class WWStackL2D : public SystemCore<WWStackL2D>
{
public:
    void update(GeneralManager&) override { record(243); }
};

TEST(SystemSequence, WriteWriteSplitInMultipleLayersIndependently)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<WWStackL1A>().writes<CompA>();
    gm.registerSystem<WWStackL1B>().writes<CompA>();
    gm.registerSystem<WWStackL2C>().reads<CompA>().writes<CompB>();
    gm.registerSystem<WWStackL2D>().reads<CompA>().writes<CompB>();
    EXPECT_NO_THROW(gm.update());

    ASSERT_NE(pos(240), -1);
    ASSERT_NE(pos(241), -1);
    ASSERT_NE(pos(242), -1);
    ASSERT_NE(pos(243), -1);
    EXPECT_LT(pos(240), pos(241));
    EXPECT_LT(pos(241), pos(242));
    EXPECT_LT(pos(242), pos(243));
}

// Shared write component plus a mutual explicit cycle must still be detected.
class WWCycleA : public SystemCore<WWCycleA>
{
public:
    void update(GeneralManager&) override {}
};
class WWCycleB : public SystemCore<WWCycleB>
{
public:
    void update(GeneralManager&) override {}
};

TEST(SystemSequence, CycleViaExplicitBetweenWriteWriteSystemsThrows)
{
    GeneralManager gm;
    gm.registerSystem<WWCycleA>().writes<CompA>().before<WWCycleB>();
    gm.registerSystem<WWCycleB>().writes<CompA>().before<WWCycleA>();
    EXPECT_THROW(gm.update(), std::runtime_error);
}

// Registration-chain specifics
class FluentDupA : public SystemCore<FluentDupA>
{
public:
    void update(GeneralManager&) override { record(250); }
};
class FluentDupB : public SystemCore<FluentDupB>
{
public:
    void update(GeneralManager&) override { record(251); }
};

TEST(SystemSequence, DuplicateDeclarationsAreHarmless)
{
    resetLog();
    GeneralManager gm;
    gm.registerSystem<FluentDupA>().writes<CompA>().writes<CompA>().before<FluentDupB>().before<FluentDupB>();
    gm.registerSystem<FluentDupB>().writes<CompA>();
    EXPECT_NO_THROW(gm.update());

    ASSERT_NE(pos(250), -1);
    ASSERT_NE(pos(251), -1);
    EXPECT_LT(pos(250), pos(251));
}

class FluentMergeSys : public SystemCore<FluentMergeSys>
{
public:
    void update(GeneralManager&) override { record(260); }
};
class FluentMergeReader : public SystemCore<FluentMergeReader>
{
public:
    void update(GeneralManager&) override { record(261); }
};

TEST(SystemSequence, RepeatedRegistrationMergesMetadata)
{
    resetLog();
    GeneralManager gm;
    // Same type registered twice — both instances share one metadata record
    gm.registerSystem<FluentMergeSys>().writes<CompA>();
    gm.registerSystem<FluentMergeSys>();
    gm.registerSystem<FluentMergeReader>().reads<CompA>();
    EXPECT_NO_THROW(gm.update());

    ASSERT_NE(pos(261), -1);
    EXPECT_LT(pos(260), pos(261));
}

} // namespace
