#include <gtest/gtest.h>

#include <Orhescyon/Jobs/JobPool.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <latch>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace Orhescyon;
using namespace std::chrono_literals;

TEST(JobPool, DefaultSizeIsHardwareConcurrencyMinusOne)
{
    JobPool pool;
    const unsigned hw = std::thread::hardware_concurrency();
    const std::size_t expected = hw <= 1 ? 1u : static_cast<std::size_t>(hw - 1);
    EXPECT_EQ(pool.threadCount(), expected);
    EXPECT_GE(pool.threadCount(), 1u);
}

TEST(JobPool, ExplicitSizeIsHonored)
{
    JobPool pool(4);
    EXPECT_EQ(pool.threadCount(), 4u);
}

TEST(JobPool, SubmitAsyncReturnsTypedResult)
{
    JobPool pool(2);
    auto result = pool.submitAsync([](int a, int b) { return a + b; }, 2, 3);
    result.wait();
    result.wait();
    EXPECT_EQ(result.status(), JobStatus::Ready);
    EXPECT_EQ(result.get(), 5);
    EXPECT_FALSE(result.valid());
}

TEST(JobPool, SubmitAsyncMovesResultAndHandle)
{
    JobPool pool(1);
    auto result = pool.submitAsync([value = std::make_unique<int>(42)]() mutable { return std::move(value); });
    auto moved = std::move(result);
    EXPECT_FALSE(result.valid());
    ASSERT_TRUE(moved.valid());
    auto value = moved.get();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 42);
    EXPECT_FALSE(moved.valid());
}

TEST(JobPool, SubmitAsyncReturnsReference)
{
    int value = 42;
    JobPool pool(1);
    auto result = pool.submitAsync([&]() -> int& { return value; });
    int& returned = result.get();
    EXPECT_EQ(&returned, &value);
    returned = 7;
    EXPECT_EQ(value, 7);
}

TEST(JobPool, SubmitAsyncVoidTask)
{
    JobPool pool(2);
    std::atomic<bool> ran{false};
    auto result = pool.submitAsync([&] { ran.store(true); });
    result.get();
    EXPECT_TRUE(ran.load());
}

TEST(JobPool, ManyTasksAllRun)
{
    constexpr int N = 1000;
    JobPool pool(4);

    std::atomic<int> counter{0};
    std::vector<JobResult<void>> results;
    results.reserve(N);
    for (int i = 0; i < N; ++i)
        results.push_back(pool.submitAsync([&] { counter.fetch_add(1, std::memory_order_relaxed); }));

    for (auto& result : results) result.get();
    EXPECT_EQ(counter.load(), N);
}

TEST(JobPool, DestructorJoinsAllThreads)
{
    std::atomic<int> counter{0};
    {
        JobPool pool(4);
        std::vector<JobResult<void>> results;
        for (int i = 0; i < 16; ++i)
            results.push_back(pool.submitAsync([&] { counter.fetch_add(1); }));
        for (auto& result : results) result.get();
    }
    EXPECT_EQ(counter.load(), 16);
}

TEST(JobPool, BlockingTasksDoNotDeadlockOnDestruction)
{
    std::latch started{4};
    std::atomic<int> finished{0};
    {
        JobPool pool(4);
        for (int i = 0; i < 4; ++i)
            pool.submitAsync([&] {
                started.count_down();
                std::this_thread::sleep_for(50ms);
                finished.fetch_add(1);
            });
        started.wait();
    }
    EXPECT_EQ(finished.load(), 4);
}

TEST(JobPool, SubmitFireAndForgetRunsTask)
{
    std::atomic<bool> ran{false};
    std::latch done{1};
    JobPool pool(2);

    pool.submit([&ran, &done] {
        ran.store(true);
        done.count_down();
    });

    done.wait();
    EXPECT_TRUE(ran.load());
}

TEST(JobPool, SubmitManyFireAndForgetAllRun)
{
    constexpr int N = 1000;
    std::atomic<int> counter{0};
    std::latch allDone{N};
    JobPool pool(4);

    for (int i = 0; i < N; ++i)
    {
        pool.submit([&counter, &allDone] {
            counter.fetch_add(1);
            allDone.count_down();
        });
    }

    allDone.wait();
    EXPECT_EQ(counter.load(), N);
}

TEST(JobPool, SubmitFireAndForgetDestructorWaitsForRunningTasks)
{
    std::atomic<int> finished{0};
    std::latch started{4};
    {
        JobPool pool(4);
        for (int i = 0; i < 4; ++i)
            pool.submit([&started, &finished] {
                started.count_down();
                std::this_thread::sleep_for(50ms);
                finished.fetch_add(1);
            });
        started.wait();
    }
    EXPECT_EQ(finished.load(), 4);
}


#if GTEST_HAS_DEATH_TEST
TEST(JobPoolDeathTest, ConstructorWithZeroThreadCountAborts)
{
    EXPECT_DEATH(JobPool{0}, "JobPool: threadCount must be at least 1");
}

TEST(JobPoolDeathTest, GetWithoutTaskStateAborts)
{
    EXPECT_DEATH(
        {
            JobResult<int> result;
            result.get();
        },
        "JobResult: no task state");
}
#endif

TEST(JobPool, QueuedTasksAreDiscardedOnPoolDestruction)
{
    JobResult<int> longResult;
    std::vector<JobResult<int>> queuedResults;
    JobGroup group;

    std::latch longStarted{1};
    std::latch queued{1};
    std::atomic<bool> release{false};
    std::atomic<int> queuedTaskCalls{0};

    std::thread owner([&] {
        JobPool pool(1);

        longResult = pool.submitAsync([&] {
            longStarted.count_down();
            release.wait(false);
            return 42;
        });

        longStarted.wait();

        for (int i = 0; i < 5; ++i)
            queuedResults.push_back(pool.submitAsync([&, i]
            {
                queuedTaskCalls.fetch_add(1);
                return i;
            }));
        group = pool.parallelForAsync(std::size_t{0}, std::size_t{4}, [&](std::size_t)
        {
            queuedTaskCalls.fetch_add(1);
        });
        queued.count_down();
    });

    queued.wait();
    for (auto& result : queuedResults) result.wait();
    release.store(true, std::memory_order_release);
    release.notify_all();
    owner.join();

    EXPECT_EQ(longResult.get(), 42);
    EXPECT_EQ(queuedTaskCalls.load(), 0);

    for (auto& result : queuedResults)
    {
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.status(), JobStatus::Cancelled);
    }
#if GTEST_HAS_DEATH_TEST
    EXPECT_DEATH(queuedResults[0].get(), "JobResult: get on cancelled task");
    EXPECT_DEATH(group.wait(), "JobGroup: wait on cancelled tasks");
#endif
}

TEST(JobPool, ParallelForCoversIndexRange)
{
    JobPool pool(4);
    constexpr std::size_t N = 1000;
    std::vector<std::atomic<int>> counts(N);

    pool.parallelFor(std::size_t{0}, N, [&](std::size_t i) {
        counts[i].fetch_add(1, std::memory_order_relaxed);
    });

    for (std::size_t i = 0; i < N; ++i) EXPECT_EQ(counts[i].load(), 1) << "index " << i;
}

TEST(JobPool, ParallelForCoversRange)
{
    JobPool pool(4);
    std::vector<int> items(1000, 0);

    pool.parallelFor(items, [](int& x) { x = 1; });

    for (int x : items) EXPECT_EQ(x, 1);
}

TEST(JobPool, ParallelForEmptyRangeIsNoOp)
{
    JobPool pool(4);
    std::atomic<int> calls{0};

    pool.parallelFor(std::size_t{0}, std::size_t{0}, [&](std::size_t) { calls.fetch_add(1); });

    EXPECT_EQ(calls.load(), 0);
}

TEST(JobPool, ParallelForFewerItemsThanThreads)
{
    JobPool pool(8);
    std::vector<int> items(3, 0);

    pool.parallelFor(items, [](int& x) { x = 1; });

    for (int x : items) EXPECT_EQ(x, 1);
}

TEST(JobPool, ParallelForMoreItemsThanThreads)
{
    JobPool pool(2);
    std::vector<int> items(100, 0);

    pool.parallelFor(items, [](int& x) { x = 1; });

    for (int x : items) EXPECT_EQ(x, 1);
}

TEST(JobPool, ParallelForActuallyRunsInParallel)
{
    JobPool pool(4);
    std::atomic<int> active{0};
    std::atomic<int> peak{0};

    pool.parallelFor(std::size_t{0}, std::size_t{4}, [&](std::size_t) {
        const int now = active.fetch_add(1) + 1;
        int prev = peak.load();
        while (now > prev && !peak.compare_exchange_weak(prev, now)) {}
        std::this_thread::sleep_for(50ms);
        active.fetch_sub(1);
    });

    EXPECT_GT(peak.load(), 1);
}

TEST(JobPool, ParallelForBarrierWaitsForAllChunks)
{
    JobPool pool(4);
    std::atomic<int> done{0};

    pool.parallelFor(std::size_t{0}, std::size_t{16}, [&](std::size_t) {
        std::this_thread::sleep_for(10ms);
        done.fetch_add(1);
    });

    EXPECT_EQ(done.load(), 16);
}

TEST(JobPool, ParallelForAsyncReturnsGroupAndWaitJoins)
{
    JobPool pool(4);
    std::atomic<int> done{0};

    auto group = pool.parallelForAsync(std::size_t{0}, std::size_t{16}, [&](std::size_t) {
        std::this_thread::sleep_for(10ms);
        done.fetch_add(1);
    });

    EXPECT_FALSE(group.empty());
    EXPECT_TRUE(group.valid());
    EXPECT_EQ(group.chunkCount(), 4u);
    group.wait();
    EXPECT_TRUE(group.empty());
    EXPECT_FALSE(group.valid());
    EXPECT_EQ(group.chunkCount(), 0u);
    EXPECT_EQ(done.load(), 16);
}

TEST(JobPool, ParallelForAsyncEmptyGroup)
{
    JobPool pool(4);

    auto group = pool.parallelForAsync(std::size_t{0}, std::size_t{0}, [](std::size_t) {});

    EXPECT_TRUE(group.empty());
    group.wait();
}

TEST(JobPool, ParallelForAsyncWaitIsIdempotent)
{
    JobPool pool(4);
    std::atomic<int> done{0};

    auto group = pool.parallelForAsync(std::size_t{0}, std::size_t{8}, [&](std::size_t) {
        done.fetch_add(1);
    });

    group.wait();
    group.wait();
    EXPECT_EQ(done.load(), 8);
}

TEST(JobPool, ParallelForAsyncDroppedGroupDoesNotCrash)
{
    std::latch done{16};
    JobPool pool(4);
    {
        auto group = pool.parallelForAsync(std::size_t{0}, std::size_t{16}, [&](std::size_t) {
            done.count_down();
        });
    }
    done.wait();
}

// Compile-check helper: builds an EXCLUDE_FROM_ALL target via cmake; success = implementation accepts the shape.
namespace
{
    struct CompileCheckResult
    {
        int exitCode;
        std::string commandLine;
        std::string output;
    };

#ifdef _WIN32
    constexpr auto popenFn  = ::_popen;
    constexpr auto pcloseFn = ::_pclose;
#else
    constexpr auto popenFn  = ::popen;
    constexpr auto pcloseFn = ::pclose;
#endif

    // Restores dev-shell env (INCLUDE/LIB/LIBPATH/PATH) that the test launcher may have stripped.
    void ensureBuildEnvLoaded()
    {
        static bool loaded = false;
        if (loaded) return;
        loaded = true;

        std::ifstream in(ORHESCYON_TEST_ENV_FILE);
        if (!in) return;

        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            const std::string key = line.substr(0, eq);
            const std::string val = line.substr(eq + 1);
#ifdef _WIN32
            ::_putenv_s(key.c_str(), val.c_str());
#else
            ::setenv(key.c_str(), val.c_str(), 1);
#endif
        }
    }

    CompileCheckResult buildCompileCheckTarget(const char* targetName)
    {
        ensureBuildEnvLoaded();

        std::string cmd;
#ifdef _WIN32
        // Outer quotes survive cmd.exe /c's quote-stripping; without them paths with spaces break.
        cmd += "\"";
#endif
        cmd += "\"";
        cmd += ORHESCYON_CMAKE_COMMAND;
        cmd += "\" --build \"";
        cmd += ORHESCYON_BUILD_DIR;
        cmd += "\" --target ";
        cmd += targetName;
        if (std::strlen(ORHESCYON_CMAKE_CONFIG) > 0)
        {
            cmd += " --config ";
            cmd += ORHESCYON_CMAKE_CONFIG;
        }
        cmd += " 2>&1";
#ifdef _WIN32
        cmd += "\"";
#endif

        CompileCheckResult result;
        result.commandLine = cmd;

        FILE* pipe = popenFn(cmd.c_str(), "r");
        if (!pipe)
        {
            result.exitCode = -1;
            result.output = "failed to spawn cmake process";
            return result;
        }

        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) result.output += buffer;

        result.exitCode = pcloseFn(pipe);
        return result;
    }
}

TEST(JobPoolRobustness, ParallelForAcceptsLvalueCallable)
{
    auto r = buildCompileCheckTarget("JobPoolCompile_LvalueCallable");
    EXPECT_EQ(r.exitCode, 0) << "command: " << r.commandLine << "\n--- output ---\n" << r.output;
}

TEST(JobPoolRobustness, ParallelForAcceptsMoveOnlyCallable)
{
    auto r = buildCompileCheckTarget("JobPoolCompile_MoveOnlyCallable");
    EXPECT_EQ(r.exitCode, 0) << "command: " << r.commandLine << "\n--- output ---\n" << r.output;
}

TEST(JobPoolRobustness, ParallelForAcceptsRandomAccessIteratorRange)
{
    auto r = buildCompileCheckTarget("JobPoolCompile_RandomAccessIteratorRange");
    EXPECT_EQ(r.exitCode, 0) << "command: " << r.commandLine << "\n--- output ---\n" << r.output;
}

TEST(JobPoolRobustness, ParallelForAcceptsForwardIteratorRange)
{
    auto r = buildCompileCheckTarget("JobPoolCompile_ForwardIteratorRange");
    EXPECT_EQ(r.exitCode, 0) << "command: " << r.commandLine << "\n--- output ---\n" << r.output;
}

TEST(JobPoolRobustness, SubmitForwardsArgs)
{
    auto r = buildCompileCheckTarget("JobPoolCompile_SubmitForwardsArgs");
    EXPECT_EQ(r.exitCode, 0) << "command: " << r.commandLine << "\n--- output ---\n" << r.output;
}

TEST(JobPoolRobustness, SubmitAcceptsMoveOnlyCallable)
{
    auto r = buildCompileCheckTarget("JobPoolCompile_SubmitMoveOnlyCallable");
    EXPECT_EQ(r.exitCode, 0) << "command: " << r.commandLine << "\n--- output ---\n" << r.output;
}

TEST(JobPoolRobustness, SubmitAcceptsMoveOnlyArg)
{
    auto r = buildCompileCheckTarget("JobPoolCompile_SubmitMoveOnlyArg");
    EXPECT_EQ(r.exitCode, 0) << "command: " << r.commandLine << "\n--- output ---\n" << r.output;
}

TEST(JobPoolRobustness, ParallelForPreservesIndexTypeInCallback)
{
    JobPool pool(2);
    std::atomic<bool> wrongType{false};

    pool.parallelFor(std::size_t{0}, std::size_t{16}, [&](auto i) {
        if constexpr (!std::is_same_v<decltype(i), std::size_t>)
            wrongType.store(true);
    });

    EXPECT_FALSE(wrongType.load());
}

TEST(JobPoolRobustness, ParallelForPreservesLargeIndexValues)
{
    JobPool pool(2);
    std::atomic<std::int64_t> sum{0};

    constexpr std::int64_t base = std::int64_t{1} << 40;
    constexpr std::int64_t count = 16;

    pool.parallelFor(base, base + count, [&](std::int64_t i) {
        sum.fetch_add(i, std::memory_order_relaxed);
    });

    std::int64_t expected = 0;
    for (std::int64_t i = base; i < base + count; ++i) expected += i;

    EXPECT_EQ(sum.load(), expected);
}
