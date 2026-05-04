#pragma once
#include <Orhescyon/Jobs/JobGroup.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Orhescyon
{
class JobPool
{
private:
	std::vector<std::thread> _workers;
	std::queue<std::function<void()>> _tasks;
	std::mutex _mutex;
	std::condition_variable _cvar;
	bool _stop = false;

	void workerLoop()
	{
		for (;;)
		{
			std::function<void()> task;

			{
				std::unique_lock<std::mutex> lock(_mutex);
				_cvar.wait(lock, [this] { return _stop || !_tasks.empty(); });

				if (_stop) return;

				task = std::move(_tasks.front());
				_tasks.pop();
			}

			task();
		}
	}

public:
	JobPool(const JobPool&) = delete;
	JobPool& operator=(const JobPool&) = delete;
	JobPool(JobPool&&) = delete;
	JobPool& operator=(JobPool&&) = delete;

	static std::size_t defaultThreadCount() noexcept
	{
		const unsigned maxThreads = std::thread::hardware_concurrency();
		if (maxThreads <= 1) return 1;
		return static_cast<std::size_t>(maxThreads - 1);
	}

	explicit JobPool(std::size_t threadCount = defaultThreadCount())
	{
		if (threadCount == 0) throw std::invalid_argument("JobPool: threadCount must be at least 1");

		_workers.reserve(threadCount);
		for (std::size_t i = 0; i < threadCount; ++i) _workers.emplace_back([this] { workerLoop(); });
	}

	~JobPool()
	{
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_stop = true;
		}
		_cvar.notify_all();
		for (auto& t : _workers)
			if (t.joinable()) t.join();
	}

	std::size_t threadCount() const noexcept
	{
		return _workers.size();
	}

	template <typename Callable, typename... Args>
	auto submit(Callable&& f, Args&&... args) -> std::future<std::invoke_result_t<Callable, Args...>>
	{
		using ReturnType = std::invoke_result_t<Callable, Args...>;

		// shared_ptr: packaged_task is move-only, std::function in the queue isn't
		auto task = std::make_shared<std::packaged_task<ReturnType()>>
		(
		    [
				func = std::forward<Callable>(f), 
				tup = std::make_tuple(std::forward<Args>(args)...)
			]() mutable -> ReturnType
		    { return std::apply(std::move(func), std::move(tup)); }
		);

		std::future<ReturnType> fut = task->get_future();
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_stop) throw std::runtime_error("JobPool: submit on stopped pool");
			_tasks.emplace([task] { (*task)(); });
		}
		_cvar.notify_one();
		return fut;
	}

	// Blocking parallel for.
	template <typename Index, typename Func>
	void parallelFor(Index begin, Index end, Func&& func)
	{
		parallelForAsync(begin, end, std::forward<Func>(func)).wait();
	}

	template <typename Range, typename Func>
	void parallelFor(Range&& range, Func&& func)
	{
		parallelForAsync(std::forward<Range>(range), std::forward<Func>(func)).wait();
	}

	// Async: caller must keep captured-by-reference state alive until JobGroup::wait().
	template <typename Index, typename Func>
	JobGroup parallelForAsync(Index begin, Index end, Func&& func)
	{
		if (!(begin < end)) return JobGroup{};

		std::size_t countIterations = end - begin;
		std::size_t threadInUse = std::min(countIterations, threadCount());

		std::size_t chunkSize = countIterations / threadInUse;
		std::size_t reminders = countIterations % threadInUse;

		std::vector<std::future<void>> futures;
		futures.reserve(threadInUse);

		auto localFunc = std::make_shared<std::decay_t<Func>>(std::forward<Func>(func));

		Index beginIteration = begin;
		for (std::size_t i = 0; i < threadInUse; ++i)
		{
			std::size_t size = chunkSize + (i < reminders ? 1 : 0);

			Index endIteration = beginIteration + size;

			futures.emplace_back(submit(
			    [localFunc, beginIteration, endIteration]
			    {
				    for (auto it = beginIteration; it != endIteration; ++it) (*localFunc)(it);
			    }));
			beginIteration = endIteration;
		}
		
		return JobGroup{std::move(futures)};
	}

	template <typename Range, typename Func>
	JobGroup parallelForAsync(Range&& range, Func&& func)
	{
		using std::begin;
		using std::end;
		auto first = begin(range);
		auto last = end(range);
		using Category = typename std::iterator_traits<decltype(first)>::iterator_category;
		return parallelForImpl(first, last, std::forward<Func>(func), Category{});
	}

private:
	// Random-access: distance/advance are O(1), split is cheap.
	template <typename It, typename Func>
	JobGroup parallelForImpl(It first, It last, Func&& func, std::random_access_iterator_tag)
	{
		if (first == last) return JobGroup{};

		std::size_t countIterations = std::distance(first, last);
		std::size_t threadInUse = std::min(countIterations, threadCount());

		std::size_t chunkSize = countIterations / threadInUse;
		std::size_t reminders = countIterations % threadInUse;

		std::vector<std::future<void>> futures;
		futures.reserve(threadInUse);

		auto localFunc = std::make_shared<std::decay_t<Func>>(std::forward<Func>(func));

		auto beginIteration = first;
		for (std::size_t i = 0; i < threadInUse; ++i)
		{
			std::size_t size = chunkSize + (i < reminders ? 1 : 0);
			auto endIteration = beginIteration + size;

			futures.emplace_back(submit(
			    [localFunc, beginIteration, endIteration]
			    {
				    for (auto it = beginIteration; it != endIteration; ++it) (*localFunc)(*it);
			    }));
			beginIteration = endIteration;
		}

		return JobGroup{std::move(futures)};
	}

	// Forward / bidirectional: collect iterators once, then split by index.
	template <typename It, typename Func>
	JobGroup parallelForImpl(It first, It last, Func&& func, std::forward_iterator_tag)
	{
		auto iters = std::make_shared<std::vector<It>>();
		for (auto it = first; it != last; ++it) iters->push_back(it);

		if (iters->empty()) return JobGroup{};

		std::size_t total = iters->size();
		std::size_t threadInUse = std::min(total, threadCount());

		std::size_t chunkSize = total / threadInUse;
		std::size_t reminders = total % threadInUse;

		std::vector<std::future<void>> futures;
		futures.reserve(threadInUse);

		auto localFunc = std::make_shared<std::decay_t<Func>>(std::forward<Func>(func));

		std::size_t cursor = 0;
		for (std::size_t i = 0; i < threadInUse; ++i)
		{
			std::size_t size = chunkSize + (i < reminders ? 1 : 0);
			std::size_t beginIdx = cursor;
			std::size_t endIdx = cursor + size;

			futures.emplace_back(submit(
			    [localFunc, iters, beginIdx, endIdx]
			    {
				    for (std::size_t k = beginIdx; k < endIdx; ++k) (*localFunc)(*(*iters)[k]);
			    }));
			cursor = endIdx;
		}

		return JobGroup{std::move(futures)};
	}
};
} // namespace Orhescyon
