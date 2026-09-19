#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Orhescyon
{
// Pointer-stable pool allocator. Blocks are never freed or moved, so pointers remain valid.
// Freed slots are recycled via an internal free list. BlockSize must be a power of two.
template <typename T, uint32_t BlockSize = 4096>
class StablePool
{
	static_assert(BlockSize > 0 && (BlockSize & BlockSize - 1) == 0, "BlockSize must be a power of two");

	static constexpr uint32_t BLOCK_SHIFT = []() constexpr
	{
		uint32_t shift = 0;
		uint32_t i = BlockSize;
		while (i > 1)
		{
			i >>= 1;
			++shift;
		}
		return shift;
	}();
	static constexpr uint32_t BLOCK_MASK = BlockSize - 1;
	static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();
	static constexpr uint64_t FREE_TAG_INCREMENT = uint64_t{1} << 32;

	struct Block
	{
		alignas(alignof(T)) unsigned char data[sizeof(T) * BlockSize];
		std::array<std::atomic<uint32_t>, BlockSize> nextFree{};

		T& at(uint32_t localIdx) noexcept
		{
			return *std::launder(reinterpret_cast<T*>(data + sizeof(T) * localIdx));
		}

		const T& at(uint32_t localIdx) const noexcept
		{
			return *std::launder(reinterpret_cast<const T*>(data + sizeof(T) * localIdx));
		}

		T* ptr(uint32_t localIdx) noexcept
		{
			return std::launder(reinterpret_cast<T*>(data + sizeof(T) * localIdx));
		}
	};

	struct BlockTable
	{
		const uint32_t size;
		std::unique_ptr<std::atomic<Block*>[]> blocks;

		explicit BlockTable(uint32_t blockCount)
			: size(blockCount), blocks(std::make_unique<std::atomic<Block*>[]>(blockCount))
		{
		}

		~BlockTable()
		{
			for (uint32_t i = 0; i < size; ++i)
			{
				delete blocks[i].load(std::memory_order_relaxed);
			}
		}
	};

	static_assert(std::atomic<BlockTable*>::is_always_lock_free && std::atomic<Block*>::is_always_lock_free
		&& std::atomic<uint32_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free,
		"StablePool requires lock-free pointer and integer atomics");

	// Tables cover [0], [1], [2..3], [4..7], ... without moving published entries during growth.
	std::array<std::atomic<BlockTable*>, 33 - BLOCK_SHIFT> _blocks{};
	std::atomic<uint64_t> _freeIndices{INVALID_INDEX};
	std::atomic<uint32_t> _capacity{0};
	std::atomic<uint32_t> _liveCount{0};
	std::atomic<uint32_t> _freeCount{0};
	std::atomic<uint32_t> _blockCount{0};

	static uint32_t tableStart(uint32_t tableIndex) noexcept
	{
		return tableIndex == 0 ? 0 : uint32_t{1} << (tableIndex - 1);
	}

	BlockTable* ensureTable(uint32_t tableIndex)
	{
		BlockTable* table = _blocks[tableIndex].load(std::memory_order_acquire);
		if (!table)
		{
			auto candidate = std::make_unique<BlockTable>(tableIndex == 0 ? 1 : tableStart(tableIndex));
			if (_blocks[tableIndex].compare_exchange_strong(table, candidate.get(),
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				table = candidate.release();
			}
		}
		return table;
	}

	Block* findBlock(uint32_t index) const noexcept
	{
		const uint32_t blockIndex = index >> BLOCK_SHIFT;
		const uint32_t tableIndex = std::bit_width(blockIndex);
		BlockTable* table = _blocks[tableIndex].load(std::memory_order_acquire);
		return table ? table->blocks[blockIndex - tableStart(tableIndex)].load(std::memory_order_acquire) : nullptr;
	}

	Block* ensureCapacity(uint32_t index)
	{
		const uint32_t blockIndex = index >> BLOCK_SHIFT;
		const uint32_t tableIndex = std::bit_width(blockIndex);
		BlockTable* table = ensureTable(tableIndex);
		auto& entry = table->blocks[blockIndex - tableStart(tableIndex)];
		Block* block = entry.load(std::memory_order_acquire);
		if (!block)
		{
			auto candidate = std::make_unique<Block>();
			if (entry.compare_exchange_strong(block, candidate.get(),
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				block = candidate.release();
			}
		}
		uint32_t count = _blockCount.load(std::memory_order_relaxed);
		while (count <= blockIndex && !_blockCount.compare_exchange_weak(count, blockIndex + 1,
			std::memory_order_relaxed))
		{
		}
		return block;
	}

	void releaseIndex(uint32_t index) noexcept
	{
		auto& next = findBlock(index)->nextFree[index & BLOCK_MASK];
		// Count the slot before publishing it so a competing pop cannot underflow the count.
		_freeCount.fetch_add(1, std::memory_order_relaxed);
		uint64_t head = _freeIndices.load(std::memory_order_acquire);
		uint64_t desired;
		do
		{
			next.store(static_cast<uint32_t>(head), std::memory_order_relaxed);
			desired = ((head + FREE_TAG_INCREMENT) & ~uint64_t{INVALID_INDEX}) | index;
		} while (!_freeIndices.compare_exchange_weak(head, desired,
			std::memory_order_acq_rel, std::memory_order_acquire));
	}

public:
	StablePool() = default;

	~StablePool()
	{
		// Note: We don't track which slots are alive, so we don't call destructors.
		// The owning storage destroys live non-trivial objects before the pool dies.
		for (auto& table : _blocks)
		{
			delete table.load(std::memory_order_relaxed);
		}
	}

	// Allocates a slot and returns {index, pointer}.
	std::pair<uint32_t, T*> allocate(T&& value)
	{
		uint32_t index;
		Block* block = nullptr;
		uint64_t head = _freeIndices.load(std::memory_order_acquire);
		for (;;)
		{
			index = static_cast<uint32_t>(head);
			if (index == INVALID_INDEX)
			{
				break;
			}
			block = findBlock(index);
			const uint32_t next = block->nextFree[index & BLOCK_MASK].load(std::memory_order_relaxed);
			const uint64_t desired = ((head + FREE_TAG_INCREMENT) & ~uint64_t{INVALID_INDEX}) | next;
			if (_freeIndices.compare_exchange_weak(head, desired,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				_freeCount.fetch_sub(1, std::memory_order_relaxed);
				break;
			}
		}

		if (index == INVALID_INDEX)
		{
			index = _capacity.load(std::memory_order_relaxed);
			for (;;)
			{
				if (index == INVALID_INDEX) [[unlikely]]
				{
					throw std::length_error("StablePool capacity exceeded");
				}
				block = ensureCapacity(index);
				if (_capacity.compare_exchange_weak(index, index + 1,
					std::memory_order_release, std::memory_order_relaxed))
				{
					break;
				}
			}
		}

		T* slot = block->ptr(index & BLOCK_MASK);
		try
		{
			new (slot) T(std::move(value));
		}
		catch (...)
		{
			releaseIndex(index);
			throw;
		}

		_liveCount.fetch_add(1, std::memory_order_relaxed);
		return {index, slot};
	}

	// Marks a slot as free for reuse. Does NOT call the destructor -
	// the caller destroys non-trivial objects before deallocating.
	void deallocate(uint32_t index)
	{
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (index >= _capacity.load(std::memory_order_acquire)) [[unlikely]]
		{
			return;
		}
#endif
		_liveCount.fetch_sub(1, std::memory_order_relaxed);
		releaseIndex(index);
	}

	T& operator[](uint32_t index) noexcept
	{
		Block* block = findBlock(index);
#if defined(ORHESCYON_HIGH_CHECK)
		const uint32_t capacity = _capacity.load(std::memory_order_acquire);
		if (index >= capacity || !block) [[unlikely]]
		{
			std::cerr << "ERROR::STABLE_POOL::operator[] index " << index << " out of bounds (capacity: " << capacity << ")" << std::endl;
			if (capacity > 0)
			{
				return findBlock(0)->at(0);
			}
			std::terminate();
		}
#endif
		return block->at(index & BLOCK_MASK);
	}

	const T& operator[](uint32_t index) const noexcept
	{
		const Block* block = findBlock(index);
#if defined(ORHESCYON_HIGH_CHECK)
		const uint32_t capacity = _capacity.load(std::memory_order_acquire);
		if (index >= capacity || !block) [[unlikely]]
		{
			std::cerr << "ERROR::STABLE_POOL::operator[] const index " << index << " out of bounds (capacity: " << capacity << ")" << std::endl;
			if (capacity > 0)
			{
				return findBlock(0)->at(0);
			}
			std::terminate();
		}
#endif
		return block->at(index & BLOCK_MASK);
	}

	T* at(uint32_t index) noexcept
	{
		Block* block = findBlock(index);
#if defined(ORHESCYON_HIGH_CHECK)
		if (index >= _capacity.load(std::memory_order_acquire) || !block) [[unlikely]]
		{
			return nullptr;
		}
#endif
		return block->ptr(index & BLOCK_MASK);
	}

	void reserveBlocks(uint32_t numBlocks)
	{
		if (numBlocks == 0)
		{
			return;
		}
		const uint32_t lastTable = std::bit_width(numBlocks - 1);
		if (lastTable >= _blocks.size()) [[unlikely]]
		{
			throw std::length_error("StablePool block count exceeds index range");
		}
		for (uint32_t i = 0; i <= lastTable; ++i)
		{
			ensureTable(i);
		}
	}

	[[nodiscard]] uint32_t liveCount() const noexcept
	{
		return _liveCount.load(std::memory_order_relaxed);
	}

	[[nodiscard]] uint32_t capacity() const noexcept
	{
		return _capacity.load(std::memory_order_acquire);
	}

	[[nodiscard]] uint32_t freeCount() const noexcept
	{
		return _freeCount.load(std::memory_order_relaxed);
	}

	[[nodiscard]] uint32_t blockCount() const noexcept
	{
		return _blockCount.load(std::memory_order_relaxed);
	}
};

}
