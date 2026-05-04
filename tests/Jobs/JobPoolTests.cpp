#include <gtest/gtest.h>

#include <Orhescyon/Jobs/JobPool.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
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
    auto fut = pool.submitAsync([](int a, int b) { return a + b; }, 2, 3);
    EXPECT_EQ(fut.get(), 5);
}

TEST(JobPool, SubmitAsyncVoidTask)
{
    JobPool pool(2);
    std::atomic<bool> ran{false};
    auto fut = pool.submitAsync([&] { ran.store(true); });
    EXPECT_NO_THROW(fut.get());
    EXPECT_TRUE(ran.load());
}

TEST(JobPool, ManyTasksAllRun)
{
    constexpr int N = 1000;
    JobPool pool(4);

    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    futures.reserve(N);
    for (int i = 0; i < N; ++i)
        futures.push_back(pool.submitAsync([&] { counter.fetch_add(1, std::memory_order_relaxed); }));

    for (auto& f : futures) f.get();
    EXPECT_EQ(counter.load(), N);
}

TEST(JobPool, ExceptionPropagatedThroughFuture)
{
    JobPool pool(1);
    auto fut = pool.submitAsync([] { throw std::runtime_error("boom"); });
    EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST(JobPool, DestructorJoinsAllThreads)
{
    auto run = std::async(std::launch::async, [] {
        JobPool pool(4);
        std::atomic<int> counter{0};
        std::vector<std::future<void>> futures;
        for (int i = 0; i < 16; ++i)
            futures.push_back(pool.submitAsync([&] { counter.fetch_add(1); }));
        for (auto& f : futures) f.get();
        EXPECT_EQ(counter.load(), 16);
    });
    ASSERT_EQ(run.wait_for(2s), std::future_status::ready) << "destructor deadlocked";
    run.get();
}

TEST(JobPool, BlockingTasksDoNotDeadlockOnDestruction)
{
    auto run = std::async(std::launch::async, [] {
        std::atomic<int> started{0};
        std::atomic<int> finished{0};
        {
            JobPool pool(4);
            for (int i = 0; i < 4; ++i)
                pool.submitAsync([&] {
                    started.fetch_add(1);
                    std::this_thread::sleep_for(50ms);
                    finished.fetch_add(1);
                });

            while (started.load() < 4) std::this_thread::sleep_for(1ms);
        }
        EXPECT_EQ(finished.load(), 4);
    });
    ASSERT_EQ(run.wait_for(3s), std::future_status::ready) << "destructor deadlocked";
    run.get();
}

TEST(JobPool, SubmitFireAndForgetRunsTask)
{
    JobPool pool(2);
    std::atomic<bool> ran{false};
    std::promise<void> done;
    auto fut = done.get_future();

    pool.submit([&ran, &done] {
        ran.store(true);
        done.set_value();
    });

    ASSERT_EQ(fut.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(ran.load());
}

TEST(JobPool, SubmitManyFireAndForgetAllRun)
{
    constexpr int N = 1000;
    JobPool pool(4);

    std::atomic<int> counter{0};
    std::promise<void> allDone;
    auto fut = allDone.get_future();

    for (int i = 0; i < N; ++i)
    {
        pool.submit([&counter, &allDone] {
            if (counter.fetch_add(1, std::memory_order_acq_rel) + 1 == N)
                allDone.set_value();
        });
    }

    ASSERT_EQ(fut.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(counter.load(), N);
}

TEST(JobPool, SubmitFireAndForgetDestructorWaitsForRunningTasks)
{
    std::atomic<int> finished{0};
    auto run = std::async(std::launch::async, [&finished] {
        std::atomic<int> started{0};
        {
            JobPool pool(4);
            for (int i = 0; i < 4; ++i)
                pool.submit([&started, &finished] {
                    started.fetch_add(1);
                    std::this_thread::sleep_for(50ms);
                    finished.fetch_add(1);
                });
            while (started.load() < 4) std::this_thread::sleep_for(1ms);
        }
    });

    ASSERT_EQ(run.wait_for(3s), std::future_status::ready) << "destructor deadlocked";
    run.get();
    EXPECT_EQ(finished.load(), 4);
}


TEST(JobPool, ConstructorRejectsZeroThreadCount)
{
    EXPECT_THROW(JobPool{0}, std::invalid_argument);
}

TEST(JobPool, QueuedTasksGetBrokenPromiseOnPoolDestruction)
{
    std::future<int> longFut;
    std::vector<std::future<int>> queuedFutures;

    std::atomic<bool> longStarted{false};
    std::atomic<bool> release{false};

    // pool lives on its own thread so we can release the long task only after ~JobPool sets _stop
    std::thread owner([&] {
        JobPool pool(1);

        longFut = pool.submitAsync([&] {
            longStarted.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(1ms);
            return 42;
        });

        while (!longStarted.load(std::memory_order_acquire))
            std::this_thread::sleep_for(1ms);

        for (int i = 0; i < 5; ++i)
            queuedFutures.push_back(pool.submitAsync([i] { return i; }));
    });

    // let owner enter ~JobPool (sets _stop) while the worker is still in the long task
    std::this_thread::sleep_for(50ms);
    release.store(true, std::memory_order_release);
    owner.join();

    EXPECT_EQ(longFut.get(), 42);

    for (auto& f : queuedFutures)
    {
        try
        {
            f.get();
            FAIL() << "expected broken_promise on queued task";
        }
        catch (const std::future_error& e)
        {
            EXPECT_EQ(e.code(), std::make_error_code(std::future_errc::broken_promise));
        }
    }
}

TEST(JobPool, ExceptionInTaskDoesNotKillWorker)
{
    JobPool pool(1);

    auto bad = pool.submitAsync([] { throw std::runtime_error("first"); });
    auto good = pool.submitAsync([] { return 7; });

    EXPECT_THROW(bad.get(), std::runtime_error);
    EXPECT_EQ(good.get(), 7);
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

TEST(JobPool, ParallelForRethrowsException)
{
    JobPool pool(4);

    EXPECT_THROW(
        pool.parallelFor(std::size_t{0}, std::size_t{100}, [](std::size_t i) {
            if (i == 50) throw std::runtime_error("boom");
        }),
        std::runtime_error
    );
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
    group.wait();
    EXPECT_EQ(done.load(), 16);
}

TEST(JobPool, ParallelForAsyncRethrowsOnWait)
{
    JobPool pool(4);

    auto group = pool.parallelForAsync(std::size_t{0}, std::size_t{100}, [](std::size_t i) {
        if (i == 50) throw std::runtime_error("boom");
    });

    EXPECT_THROW(group.wait(), std::runtime_error);
}

TEST(JobPool, ParallelForAsyncEmptyGroup)
{
    JobPool pool(4);

    auto group = pool.parallelForAsync(std::size_t{0}, std::size_t{0}, [](std::size_t) {});

    EXPECT_TRUE(group.empty());
    EXPECT_NO_THROW(group.wait());
}

TEST(JobPool, ParallelForAsyncWaitIsIdempotent)
{
    JobPool pool(4);
    std::atomic<int> done{0};

    auto group = pool.parallelForAsync(std::size_t{0}, std::size_t{8}, [&](std::size_t) {
        done.fetch_add(1);
    });

    group.wait();
    EXPECT_NO_THROW(group.wait());
    EXPECT_EQ(done.load(), 8);
}

TEST(JobPool, ParallelForAsyncDroppedGroupDoesNotCrash)
{
    auto run = std::async(std::launch::async, [] {
        JobPool pool(4);
        std::atomic<int> done{0};

        {
            auto group = pool.parallelForAsync(std::size_t{0}, std::size_t{16}, [&](std::size_t) {
                done.fetch_add(1);
            });
        }
    });

    ASSERT_EQ(run.wait_for(2s), std::future_status::ready);
    run.get();
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
