#include <gtest/gtest.h>

#include <Orhescyon/Jobs/DefaultJobSystem.hpp>

#include <atomic>
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
