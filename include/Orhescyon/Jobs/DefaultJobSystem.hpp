#pragma once

#include <Orhescyon/Jobs/IJobSystem.hpp>
#include <Orhescyon/Jobs/JobPool.hpp>

namespace Orhescyon
{
class DefaultJobSystem final : public IJobSystem
{
private:
	JobPool _pool;

public:
	explicit DefaultJobSystem(std::size_t threadCount = JobPool::defaultThreadCount()) : _pool(threadCount)
	{
	}

	void parallelFor(std::size_t count, const std::function<void(std::size_t)>& body) override
	{
		_pool.parallelFor(std::size_t{0}, count, body);
	}
};
} // namespace Orhescyon
