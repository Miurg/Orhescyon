#pragma once

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

	[[nodiscard]] bool empty() const noexcept
	{
		return _futures.empty();
	}

	[[nodiscard]] std::size_t chunkCount() const noexcept
	{
		return _futures.size();
	}

	[[nodiscard]] bool valid() const noexcept
	{
		if (_futures.empty()) return false;
		for (const auto& f : _futures)
			if (!f.valid()) return false;
		return true;
	}

	// Idempotent: clears futures on completion, second call is a no-op.
	void wait()
	{
		std::vector<std::future<void>> futures;
		futures.swap(_futures);
		// Wait for every task before an exception from get() can unwind the caller.
		for (auto& f : futures)
		{
			if (f.valid()) f.wait();
		}
		for (auto& f : futures)
		{
			if (f.valid()) f.get();
		}
	}
};

} // namespace Orhescyon
