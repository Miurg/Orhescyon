#include <gtest/gtest.h>

#include <Orhescyon/Jobs/JobPool.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
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

TEST(JobPool, SubmitReturnsTypedResult)
{
    JobPool pool(2);
    auto fut = pool.submit([](int a, int b) { return a + b; }, 2, 3);
    EXPECT_EQ(fut.get(), 5);
}

TEST(JobPool, SubmitVoidTask)
{
    JobPool pool(2);
    std::atomic<bool> ran{false};
    auto fut = pool.submit([&] { ran.store(true); });
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
        futures.push_back(pool.submit([&] { counter.fetch_add(1, std::memory_order_relaxed); }));

    for (auto& f : futures) f.get();
    EXPECT_EQ(counter.load(), N);
}

TEST(JobPool, ExceptionPropagatedThroughFuture)
{
    JobPool pool(1);
    auto fut = pool.submit([] { throw std::runtime_error("boom"); });
    EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST(JobPool, DestructorJoinsAllThreads)
{
    auto run = std::async(std::launch::async, [] {
        JobPool pool(4);
        std::atomic<int> counter{0};
        std::vector<std::future<void>> futures;
        for (int i = 0; i < 16; ++i)
            futures.push_back(pool.submit([&] { counter.fetch_add(1); }));
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
                pool.submit([&] {
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

        longFut = pool.submit([&] {
            longStarted.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(1ms);
            return 42;
        });

        while (!longStarted.load(std::memory_order_acquire))
            std::this_thread::sleep_for(1ms);

        for (int i = 0; i < 5; ++i)
            queuedFutures.push_back(pool.submit([i] { return i; }));
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

    auto bad = pool.submit([] { throw std::runtime_error("first"); });
    auto good = pool.submit([] { return 7; });

    EXPECT_THROW(bad.get(), std::runtime_error);
    EXPECT_EQ(good.get(), 7);
}
