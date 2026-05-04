#include <gtest/gtest.h>
#define ORHESCYON_HIGH_CHECK
#include <Orhescyon/GeneralManager.hpp>
#include <Orhescyon/Jobs/IJobSystem.hpp>
#include <Orhescyon/Systems/SystemCore.hpp>

#include <atomic>

using namespace Orhescyon;

namespace
{
class CountingJobSystem : public IJobSystem
{
public:
	std::atomic<std::size_t> callCount{0};
	std::atomic<std::size_t> lastCount{0};

	void parallelFor(std::size_t count, const std::function<void(std::size_t)>& body) override
	{
		callCount.fetch_add(1, std::memory_order_relaxed);
		lastCount.store(count, std::memory_order_relaxed);
		for (std::size_t index = 0; index < count; ++index) body(index);
	}
};

std::atomic<int> g_systemAlphaRunCount{0};
std::atomic<int> g_systemBetaRunCount{0};

class SystemAlpha : public SystemCore<SystemAlpha>
{
public:
	void update(GeneralManager&) override
	{
		g_systemAlphaRunCount.fetch_add(1, std::memory_order_relaxed);
	}
};

class SystemBeta : public SystemCore<SystemBeta>
{
public:
	void update(GeneralManager&) override
	{
		g_systemBetaRunCount.fetch_add(1, std::memory_order_relaxed);
	}
};

class SystemSolo : public SystemCore<SystemSolo>
{
public:
	void update(GeneralManager&) override
	{
	}
};

void resetRunCounters()
{
	g_systemAlphaRunCount.store(0);
	g_systemBetaRunCount.store(0);
}
} // namespace

TEST(GMJobSystem, CustomJobSystem_IsUsedForParallelLayer)
{
	resetRunCounters();
	CountingJobSystem counting;
	GeneralManager gm(&counting);
	gm.registerSystem<SystemAlpha>();
	gm.registerSystem<SystemBeta>();

	gm.update();

	EXPECT_EQ(counting.callCount.load(), 1u);
	EXPECT_EQ(counting.lastCount.load(), 2u);
	EXPECT_EQ(g_systemAlphaRunCount.load(), 1);
	EXPECT_EQ(g_systemBetaRunCount.load(), 1);
}

TEST(GMJobSystem, SingleSystemLayer_DoesNotCallJobSystem)
{
	CountingJobSystem counting;
	GeneralManager gm(&counting);
	gm.registerSystem<SystemSolo>();

	gm.update();

	EXPECT_EQ(counting.callCount.load(), 0u);
}

TEST(GMJobSystem, DefaultJobSystem_RunsParallelLayer)
{
	resetRunCounters();
	GeneralManager gm;
	gm.registerSystem<SystemAlpha>();
	gm.registerSystem<SystemBeta>();

	gm.update();

	EXPECT_EQ(g_systemAlphaRunCount.load(), 1);
	EXPECT_EQ(g_systemBetaRunCount.load(), 1);
}

TEST(GMJobSystem, SetJobSystem_SwapsBackend)
{
	resetRunCounters();
	GeneralManager gm;
	gm.registerSystem<SystemAlpha>();
	gm.registerSystem<SystemBeta>();
	gm.update();
	EXPECT_EQ(g_systemAlphaRunCount.load(), 1);

	CountingJobSystem counting;
	gm.setJobSystem(&counting);

	gm.update();

	EXPECT_EQ(counting.callCount.load(), 1u);
	EXPECT_EQ(counting.lastCount.load(), 2u);
	EXPECT_EQ(g_systemAlphaRunCount.load(), 2);
	EXPECT_EQ(g_systemBetaRunCount.load(), 2);
}

TEST(GMJobSystem, SetJobSystemNull_RestoresDefault)
{
	resetRunCounters();
	CountingJobSystem counting;
	GeneralManager gm(&counting);
	gm.registerSystem<SystemAlpha>();
	gm.registerSystem<SystemBeta>();

	gm.setJobSystem(nullptr);
	gm.update();

	EXPECT_EQ(counting.callCount.load(), 0u);
	EXPECT_EQ(g_systemAlphaRunCount.load(), 1);
	EXPECT_EQ(g_systemBetaRunCount.load(), 1);
}
