#pragma once

#include <exception>
#include <future>
#include <utility>
#include <vector>

namespace Orhescyon
{
// Aggregate of futures from parallelForAsync.
class JobGroup
{
private:
	std::vector<std::future<void>> _futures;

public:
	JobGroup() = default;

	explicit JobGroup(std::vector<std::future<void>>&& futures) noexcept : _futures(std::move(futures)) {}

	JobGroup(const JobGroup&) = delete;
	JobGroup& operator=(const JobGroup&) = delete;
	JobGroup(JobGroup&&) noexcept = default;
	JobGroup& operator=(JobGroup&&) noexcept = default;

	bool empty() const noexcept
	{
		return _futures.empty();
	}

	std::size_t chunkCount() const noexcept
	{
		return _futures.size();
	}

	bool valid() const noexcept
	{
		if (_futures.empty()) return false;
		for (const auto& f : _futures)
			if (!f.valid()) return false;
		return true;
	}

	// Idempotent: clears futures on completion, second call is a no-op.
	void wait()
	{
		std::exception_ptr first;
		for (auto& f : _futures)
		{
			if (!f.valid()) continue;
			try
			{
				f.get();
			}
			catch (...)
			{
				if (!first) first = std::current_exception();
			}
		}
		_futures.clear();
		if (first) std::rethrow_exception(first);
	}
};

} // namespace Orhescyon
