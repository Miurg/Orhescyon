#pragma once
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
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
				fn = std::forward<Callable>(f), 
				tup = std::make_tuple(std::forward<Args>(args)...)
			]() mutable -> ReturnType
		    { return std::apply(std::move(fn), std::move(tup)); }
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
};

} // namespace Orhescyon
