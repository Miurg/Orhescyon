#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

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
	static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();

	struct SparsePage
	{
		const uint32_t size;
		std::unique_ptr<std::atomic<uint32_t>[]> indices;

		explicit SparsePage(uint32_t count)
			: size(count), indices(std::make_unique<std::atomic<uint32_t>[]>(count))
		{
			for (uint32_t i = 0; i < size; ++i)
			{
				indices[i].store(INVALID_INDEX, std::memory_order_relaxed);
			}
		}
	};

	static_assert(std::atomic<SparsePage*>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free,
		"SparseComponentStorage requires lock-free pointer and index atomics");

	StablePool<TComponent, PoolBlockSize> _pool;
	// Pages cover [0], [1], [2..3], ... without moving entries during growth.
	std::array<std::atomic<SparsePage*>, 33> _sparse{};
	SlotBitmap _presence;

	static uint32_t sparsePageStart(uint32_t pageIndex) noexcept
	{
		return pageIndex == 0 ? 0 : uint32_t{1} << (pageIndex - 1);
	}

	std::atomic<uint32_t>& ensureSparseEntry(uint32_t slot)
	{
		const uint32_t pageIndex = std::bit_width(slot);
		const uint32_t start = sparsePageStart(pageIndex);
		SparsePage* page = _sparse[pageIndex].load(std::memory_order_acquire);
		if (!page)
		{
			auto candidate = std::make_unique<SparsePage>(pageIndex == 0 ? 1 : start);
			if (_sparse[pageIndex].compare_exchange_strong(page, candidate.get(),
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				page = candidate.release();
			}
		}
		return page->indices[slot - start];
	}

	std::atomic<uint32_t>& sparseEntry(uint32_t slot) noexcept
	{
		const uint32_t pageIndex = std::bit_width(slot);
		SparsePage* page = _sparse[pageIndex].load(std::memory_order_acquire);
		return page->indices[slot - sparsePageStart(pageIndex)];
	}

public:
	SparseComponentStorage() = default;

	~SparseComponentStorage()
	{
		if constexpr (!std::is_trivially_destructible_v<TComponent>)
		{
			_presence.forEachSetBit([this](uint32_t slot)
				{ _pool[sparseEntry(slot).load(std::memory_order_acquire)].~TComponent(); });
		}
		for (auto& page : _sparse)
		{
			delete page.load(std::memory_order_relaxed);
		}
	}

	TComponent* addComponent(Entity entity, TComponent&& component)
	{
		const uint32_t slot = entity.slot;
		auto& entry = ensureSparseEntry(slot);

		if (_presence.test(slot))
		{
			// Overwrite in place — the pool slot already holds a live object
			const uint32_t index = entry.load(std::memory_order_acquire);
			_pool[index] = std::move(component);
			return _pool.at(index);
		}

		auto [newIndex, pointer] = _pool.allocate(std::move(component));
		entry.store(newIndex, std::memory_order_release);
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
		return _pool.at(sparseEntry(entity.slot).load(std::memory_order_acquire));
	}

	void removeComponent(Entity entity)
	{
		const uint32_t slot = entity.slot;
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (!_presence.test(slot)) [[unlikely]]
			return;
#endif

		auto& entry = sparseEntry(slot);
		const uint32_t poolIndex = entry.load(std::memory_order_acquire);
		if constexpr (!std::is_trivially_destructible_v<TComponent>)
		{
			_pool[poolIndex].~TComponent();
		}
		_pool.deallocate(poolIndex);
		entry.store(INVALID_INDEX, std::memory_order_release);
		_presence.clear(slot);
	}

	// Unchecked — caller guarantees the presence bit is set.
	[[nodiscard]] TComponent* componentPointerForSlot(uint32_t slot) noexcept
	{
		return _pool.at(sparseEntry(slot).load(std::memory_order_acquire));
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
		for (const auto& entry : _sparse)
		{
			if (const SparsePage* page = entry.load(std::memory_order_acquire))
			{
				stats.indexOverheadBytes += static_cast<size_t>(page->size) * sizeof(std::atomic<uint32_t>);
			}
		}
		return stats;
	}
};

}
