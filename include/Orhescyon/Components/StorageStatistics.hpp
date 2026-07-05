#pragma once

#include <cstddef>
#include <cstdint>

namespace Orhescyon
{
// Snapshot of one component type's storage footprint.
struct StorageStatistics
{
	uint32_t liveComponentCount = 0;
	uint32_t allocatedBlockCount = 0;
	uint32_t slotsPerBlock = 0;
	size_t allocatedComponentBytes = 0;
	size_t liveComponentBytes = 0;
	size_t indexOverheadBytes = 0; // sparse-policy index vector; zero for columns
};
}
