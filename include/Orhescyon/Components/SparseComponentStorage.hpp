#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "../Entitys/Entity.hpp"
#include "../Entitys/SlotBitmap.hpp"
#include "StablePool.hpp"
#include "StorageStatistics.hpp"

namespace Orhescyon
{
// Sparse storage: slot -> pool index indirection over a pointer-stable pool. Pays one
// extra hop on access but allocates data only for live components.
template <typename TComponent, uint32_t PoolBlockSize = 4096>
class SparseComponentStorage
{
private:
	StablePool<TComponent, PoolBlockSize> _pool;
	std::vector<uint32_t> _sparse;
	SlotBitmap _presence;

	static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();

public:
	SparseComponentStorage() = default;

	~SparseComponentStorage()
	{
		if constexpr (!std::is_trivially_destructible_v<TComponent>)
		{
			_presence.forEachSetBit([this](uint32_t slot) { _pool[_sparse[slot]].~TComponent(); });
		}
	}

	TComponent* addComponent(Entity entity, TComponent&& component)
	{
		const uint32_t slot = entity.slot;
		if (slot >= _sparse.size())
		{
			_sparse.resize(static_cast<size_t>(slot) * 2 + 1, INVALID_INDEX);
		}

		if (_presence.test(slot))
		{
			// Overwrite in place — the pool slot already holds a live object
			const uint32_t index = _sparse[slot];
			_pool[index] = std::move(component);
			return _pool.at(index);
		}

		auto [newIndex, pointer] = _pool.allocate(std::move(component));
		_sparse[slot] = newIndex;
		_presence.set(slot);
		return pointer;
	}

	[[nodiscard]] bool hasComponent(Entity entity) const noexcept
	{
		return _presence.test(entity.slot);
	}

	TComponent* getComponent(Entity entity) noexcept
	{
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (!_presence.test(entity.slot)) [[unlikely]]
		{
			return nullptr;
		}
#endif
		return _pool.at(_sparse[entity.slot]);
	}

	void removeComponent(Entity entity)
	{
		const uint32_t slot = entity.slot;
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (!_presence.test(slot)) [[unlikely]]
			return;
#endif

		const uint32_t poolIndex = _sparse[slot];
		if constexpr (!std::is_trivially_destructible_v<TComponent>)
		{
			_pool[poolIndex].~TComponent();
		}
		_pool.deallocate(poolIndex);
		_sparse[slot] = INVALID_INDEX;
		_presence.clear(slot);
	}

	// Unchecked — caller guarantees the presence bit is set.
	[[nodiscard]] TComponent* componentPointerForSlot(uint32_t slot) noexcept
	{
		return _pool.at(_sparse[slot]);
	}

	// Pool indices are not slot-ordered
	static constexpr bool CONTIGUOUS_DATA = false;

	[[nodiscard]] uint64_t presenceWord(uint32_t wordIndex) const noexcept
	{
		return _presence.word(wordIndex);
	}

	[[nodiscard]] uint32_t presenceWordCount() const noexcept
	{
		return _presence.wordCount();
	}

	[[nodiscard]] size_t size() const noexcept
	{
		return _pool.liveCount();
	}

	// Pre-allocates pool block bookkeeping for the expected component count.
	void reserve(size_t capacity)
	{
		const uint32_t numBlocks = static_cast<uint32_t>((capacity + PoolBlockSize - 1) / PoolBlockSize);
		_pool.reserveBlocks(numBlocks);
	}

	[[nodiscard]] StorageStatistics statistics() const noexcept
	{
		StorageStatistics stats;
		stats.liveComponentCount = _pool.liveCount();
		stats.allocatedBlockCount = _pool.blockCount();
		stats.slotsPerBlock = PoolBlockSize;
		stats.allocatedComponentBytes = static_cast<size_t>(_pool.blockCount()) * PoolBlockSize * sizeof(TComponent);
		stats.liveComponentBytes = static_cast<size_t>(_pool.liveCount()) * sizeof(TComponent);
		stats.indexOverheadBytes = _sparse.capacity() * sizeof(uint32_t);
		return stats;
	}
};

}
