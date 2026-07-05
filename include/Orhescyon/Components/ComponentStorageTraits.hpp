#pragma once

#include <concepts>
#include <cstdint>

namespace Orhescyon
{
enum class StoragePolicy
{
	Column, // slot-indexed blocks - direct access, dense iteration; the default
	Sparse  // index vector over a stable pool - for big components on few entities
};
//
// Compile-time storage selection, resolved at the first use of the component type.
// 
// A component opts out of Column by declaring a marker in its own definition:
//     static constexpr auto orhescyonStoragePolicy = Orhescyon::StoragePolicy::Sparse;
//
template <typename TComponent>
struct ComponentStorageTraits
{
	static constexpr StoragePolicy policy = []() constexpr
	{
		if constexpr (requires { { TComponent::orhescyonStoragePolicy } -> std::convertible_to<StoragePolicy>; })
		{
			return TComponent::orhescyonStoragePolicy;
		}
		else
		{
			return StoragePolicy::Column;
		}
	}();

	static constexpr uint32_t blockSize = 4096;
};
}
