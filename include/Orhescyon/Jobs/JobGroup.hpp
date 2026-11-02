#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>

namespace Orhescyon
{
class JobGroup
{
private:
	friend class JobPool;

	struct State
	{
		const std::size_t chunks;
		std::atomic<std::size_t> remaining;
		std::atomic<bool> cancelled{false};

		explicit State(std::size_t count) : chunks(count), remaining(count) {}

		void finish(bool wasCancelled)
		{
			if (wasCancelled) cancelled.store(true, std::memory_order_relaxed);
			if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) remaining.notify_all();
		}
	};

	std::shared_ptr<State> _state;

	explicit JobGroup(std::size_t chunks) : _state(std::make_shared<State>(chunks)) {}

public:
	JobGroup() = default;

	JobGroup(const JobGroup&) = delete;
	JobGroup& operator=(const JobGroup&) = delete;
	JobGroup(JobGroup&&) noexcept = default;
	JobGroup& operator=(JobGroup&&) noexcept = default;

	[[nodiscard]] bool empty() const noexcept
	{
		return !_state;
	}

	[[nodiscard]] std::size_t chunkCount() const noexcept
	{
		return _state ? _state->chunks : 0;
	}

	[[nodiscard]] bool valid() const noexcept
	{
		return static_cast<bool>(_state);
	}

	void wait()
	{
		if (!_state) return;
		auto state = std::move(_state);
		auto remaining = state->remaining.load(std::memory_order_acquire);
		while (remaining != 0)
		{
			state->remaining.wait(remaining, std::memory_order_acquire);
			remaining = state->remaining.load(std::memory_order_acquire);
		}
		if (state->cancelled.load(std::memory_order_relaxed))
		{
			std::cerr << "JobGroup: wait on cancelled tasks" << std::endl;
			std::abort();
		}
	}
};

} // namespace Orhescyon
