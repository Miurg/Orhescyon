#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>

namespace Orhescyon
{
// Entity handle. slot directly indexes all per-entity storage; generation makes
// handles to a recycled slot detectably stale.
struct Entity
{
	uint32_t slot;
	uint32_t generation;

	[[nodiscard]] static constexpr Entity invalid() noexcept
	{
		return {std::numeric_limits<uint32_t>::max(), 0};
	}

	[[nodiscard]] constexpr bool operator==(const Entity&) const noexcept = default;
};

// Diagnostic formatting for warnings and test failures.
inline std::ostream& operator<<(std::ostream& stream, Entity entity)
{
	return stream << "{slot=" << entity.slot << ", generation=" << entity.generation << "}";
}

}

template <>
struct std::hash<Orhescyon::Entity>
{
	size_t operator()(const Orhescyon::Entity& entity) const noexcept
	{
		return std::hash<uint64_t>{}(static_cast<uint64_t>(entity.slot) << 32 | entity.generation);
	}
};
