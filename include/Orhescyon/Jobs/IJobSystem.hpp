#pragma once

#include <cstddef>
#include <functional>

namespace Orhescyon
{
class IJobSystem
{
public:
	virtual ~IJobSystem() = default;

	// Blocks until all body(i) for i in [0, count) complete.
	virtual void parallelFor(std::size_t count, const std::function<void(std::size_t)>& body) = 0;
};
} // namespace Orhescyon
