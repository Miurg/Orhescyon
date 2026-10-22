#pragma once

#include <Orhescyon/Jobs/IJobSystem.hpp>
#include <Orhescyon/Jobs/JobPool.hpp>

namespace Orhescyon
{
class DefaultJobSystem final : public IJobSystem
{
private:
	JobPool _pool;
	inline static thread_local bool _executingJob = false;

	struct WorkerScope
	{
		WorkerScope()
		{
			_executingJob = true;
		}

		~WorkerScope()
		{
			_executingJob = false;
		}
	};

public:
	explicit DefaultJobSystem(std::size_t threadCount = JobPool::defaultThreadCount()) : _pool(threadCount)
	{
	}

	void parallelFor(std::size_t count, const std::function<void(std::size_t)>& body) override
	{
		// Waiting for more pool work from a worker could exhaust the pool, so nested calls run inline.
		if (_executingJob)
		{
			for (std::size_t index = 0; index < count; ++index) body(index);
			return;
		}

		_pool.parallelFor(std::size_t{0}, count,
		                  [&body](std::size_t index)
		                  {
			                  WorkerScope scope;
			                  body(index);
		                  });
	}
};
} // namespace Orhescyon
