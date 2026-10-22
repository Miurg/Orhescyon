#include <gtest/gtest.h>

#include <Orhescyon/Jobs/DefaultJobSystem.hpp>

#include <atomic>
#include <stdexcept>
#include <vector>

using namespace Orhescyon;

TEST(DefaultJobSystem, ParallelForCoversAllIndices)
{
	DefaultJobSystem jobSystem;

	constexpr std::size_t total = 1000;
	std::vector<std::atomic<bool>> visited(total);
	for (auto& flag : visited) flag.store(false);

	jobSystem.parallelFor(total, [&visited](std::size_t index) { visited[index].store(true); });

	for (std::size_t index = 0; index < total; ++index) EXPECT_TRUE(visited[index].load()) << "index " << index;
}

TEST(DefaultJobSystem, ParallelForZeroCount_NoOp)
{
	DefaultJobSystem jobSystem;
	std::atomic<int> bodyCalls{0};

	jobSystem.parallelFor(0, [&bodyCalls](std::size_t) { bodyCalls.fetch_add(1); });

	EXPECT_EQ(bodyCalls.load(), 0);
}

TEST(DefaultJobSystem, ParallelForOneCount_RunsOnce)
{
	DefaultJobSystem jobSystem;
	std::atomic<int> bodyCalls{0};
	std::atomic<std::size_t> seenIndex{static_cast<std::size_t>(-1)};

	jobSystem.parallelFor(1,
	                      [&bodyCalls, &seenIndex](std::size_t index)
	                      {
		                      bodyCalls.fetch_add(1);
		                      seenIndex.store(index);
	                      });

	EXPECT_EQ(bodyCalls.load(), 1);
	EXPECT_EQ(seenIndex.load(), 0u);
}

TEST(DefaultJobSystem, NestedParallelForCompletesWithOneWorker)
{
	DefaultJobSystem jobSystem(1);
	std::atomic<int> bodyCalls{0};

	jobSystem.parallelFor(4, [&](std::size_t)
	{
		jobSystem.parallelFor(7, [&](std::size_t)
		{
			jobSystem.parallelFor(3, [&](std::size_t) { bodyCalls.fetch_add(1); });
		});
	});

	EXPECT_EQ(bodyCalls.load(), 84);
}

TEST(DefaultJobSystem, NestedParallelForPropagatesExceptions)
{
	DefaultJobSystem jobSystem(1);
	EXPECT_THROW(jobSystem.parallelFor(1, [&](std::size_t)
	{
		jobSystem.parallelFor(1, [](std::size_t) { throw std::runtime_error("nested job failure"); });
	}), std::runtime_error);

	std::atomic<int> bodyCalls{0};
	jobSystem.parallelFor(2, [&](std::size_t)
	{
		jobSystem.parallelFor(3, [&](std::size_t) { bodyCalls.fetch_add(1); });
	});
	EXPECT_EQ(bodyCalls.load(), 6);
}
