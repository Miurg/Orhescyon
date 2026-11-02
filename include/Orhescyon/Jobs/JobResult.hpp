#pragma once

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace Orhescyon
{
enum class JobStatus { Pending, Ready, Cancelled };

namespace detail
{
template <typename T>
struct JobResultStorage
{
	std::optional<T> value;
};

template <typename T>
struct JobResultStorage<T&>
{
	T* value = nullptr;
};

template <>
struct JobResultStorage<void> {};
}

template <typename T>
class JobResult
{
private:
	friend class JobPool;

	struct State : detail::JobResultStorage<T>
	{
		std::atomic<JobStatus> status{JobStatus::Pending};

		void finish(JobStatus finalStatus)
		{
			status.store(finalStatus, std::memory_order_release);
			status.notify_all();
		}
	};

	std::shared_ptr<State> _state;

	explicit JobResult(std::shared_ptr<State> state) : _state(std::move(state)) {}

	void requireValid() const
	{
		if (!_state)
		{
			std::cerr << "JobResult: no task state" << std::endl;
			std::abort();
		}
	}

public:
	JobResult() = default;
	JobResult(const JobResult&) = delete;
	JobResult& operator=(const JobResult&) = delete;
	JobResult(JobResult&&) noexcept = default;
	JobResult& operator=(JobResult&&) noexcept = default;

	[[nodiscard]] bool valid() const noexcept { return static_cast<bool>(_state); }

	[[nodiscard]] JobStatus status() const
	{
		requireValid();
		return _state->status.load(std::memory_order_acquire);
	}

	void wait() const
	{
		requireValid();
		auto current = _state->status.load(std::memory_order_acquire);
		while (current == JobStatus::Pending)
		{
			_state->status.wait(current, std::memory_order_acquire);
			current = _state->status.load(std::memory_order_acquire);
		}
	}

	T get()
	{
		wait();
		auto state = std::move(_state);
		if (state->status.load(std::memory_order_acquire) == JobStatus::Cancelled)
		{
			std::cerr << "JobResult: get on cancelled task" << std::endl;
			std::abort();
		}
		if constexpr (std::is_lvalue_reference_v<T>)
			return *state->value;
		else if constexpr (!std::is_void_v<T>)
			return std::move(*state->value);
	}
};
}
