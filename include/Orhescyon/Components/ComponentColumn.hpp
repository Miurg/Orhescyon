#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../Entitys/Entity.hpp"
#include "../Entitys/SlotBitmap.hpp"
#include "StorageStatistics.hpp"

namespace Orhescyon
{
// Column storage: component data indexed directly by the entity slot. Blocks are
// allocated lazily and never move.
// Empty trivially-destructible types (tags) skip block allocation entirely.
template <typename TComponent, uint32_t BlockSize = 4096>
class ComponentColumn
{
	static_assert(BlockSize >= 64 && (BlockSize & BlockSize - 1) == 0,
	              "BlockSize must be a power of two and at least 64 so presence words never straddle blocks");

	static constexpr uint32_t BLOCK_SHIFT = static_cast<uint32_t>(std::countr_zero(BlockSize));
	static constexpr uint32_t BLOCK_MASK = BlockSize - 1;

	// Tags carry no state — presence bits are the whole storage
	static constexpr bool STORES_DATA = !(std::is_empty_v<TComponent> && std::is_trivially_destructible_v<TComponent>);

	struct Block
	{
		alignas(alignof(TComponent)) unsigned char data[sizeof(TComponent) * BlockSize];

		TComponent* pointerTo(uint32_t localIndex) noexcept
		{
			return std::launder(reinterpret_cast<TComponent*>(data + sizeof(TComponent) * localIndex));
		}
	};

	struct BlockEntry
	{
		std::atomic<Block*> block{nullptr};
		std::atomic<uint32_t> liveCount{0};
	};

	struct BlockTable
	{
		const uint32_t size;
		std::unique_ptr<BlockEntry[]> entries;

		explicit BlockTable(uint32_t blockCount)
			: size(blockCount), entries(std::make_unique<BlockEntry[]>(blockCount))
		{
		}

		~BlockTable()
		{
			for (uint32_t i = 0; i < size; ++i)
			{
				delete entries[i].block.load(std::memory_order_relaxed);
			}
		}
	};

	static_assert(std::atomic<BlockTable*>::is_always_lock_free && std::atomic<Block*>::is_always_lock_free
		&& std::atomic<uint32_t>::is_always_lock_free,
		"ComponentColumn requires lock-free pointer and integer atomics");

	std::array<std::atomic<BlockTable*>, 33 - BLOCK_SHIFT> _blocks{};
	SlotBitmap _presence;
	std::atomic<uint32_t> _liveCount{0};

	// All tag instances are interchangeable, so every slot shares one address
	static TComponent* sharedTagInstance() noexcept
	{
		static TComponent instance{};
		return &instance;
	}

	static uint32_t tableStart(uint32_t tableIndex) noexcept
	{
		return tableIndex == 0 ? 0 : uint32_t{1} << (tableIndex - 1);
	}

	BlockEntry& ensureBlockEntry(uint32_t blockIndex)
	{
		const uint32_t tableIndex = std::bit_width(blockIndex);
		const uint32_t start = tableStart(tableIndex);
		BlockTable* table = _blocks[tableIndex].load(std::memory_order_acquire);
		if (!table)
		{
			auto candidate = std::make_unique<BlockTable>(tableIndex == 0 ? 1 : start);
			if (_blocks[tableIndex].compare_exchange_strong(table, candidate.get(),
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				table = candidate.release();
			}
		}
		return table->entries[blockIndex - start];
	}

	BlockEntry& blockEntry(uint32_t blockIndex) const noexcept
	{
		const uint32_t tableIndex = std::bit_width(blockIndex);
		BlockTable* table = _blocks[tableIndex].load(std::memory_order_acquire);
		return table->entries[blockIndex - tableStart(tableIndex)];
	}

	// ensure that block by index exist and if not - create that block
	Block& ensureBlock(uint32_t blockIndex)
	{
		auto& entry = ensureBlockEntry(blockIndex).block;
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
		return *block;
	}

	void markPresent(uint32_t slot)
	{
		BlockEntry& entry = ensureBlockEntry(slot >> BLOCK_SHIFT);
		_presence.set(slot);
		entry.liveCount.fetch_add(1, std::memory_order_relaxed);
		_liveCount.fetch_add(1, std::memory_order_relaxed);
	}

	void markAbsent(uint32_t slot) noexcept
	{
		_presence.clear(slot);
		blockEntry(slot >> BLOCK_SHIFT).liveCount.fetch_sub(1, std::memory_order_relaxed);
		_liveCount.fetch_sub(1, std::memory_order_relaxed);
	}

public:
	ComponentColumn() = default;

	~ComponentColumn()
	{
		if constexpr (STORES_DATA && !std::is_trivially_destructible_v<TComponent>)
		{
			_presence.forEachSetBit([this](uint32_t slot)
			                        { componentPointerForSlot(slot)->~TComponent(); });
		}
		for (auto& table : _blocks)
		{
			delete table.load(std::memory_order_relaxed);
		}
	}

	TComponent* addComponent(Entity entity, TComponent&& component)
	{
		const uint32_t slot = entity.slot;

		if constexpr (!STORES_DATA)
		{
			if (!_presence.test(slot))
			{
				markPresent(slot);
			}
			return sharedTagInstance();
		}
		else
		{
			TComponent* pointer = ensureBlock(slot >> BLOCK_SHIFT).pointerTo(slot & BLOCK_MASK);
			if (_presence.test(slot))
			{
				// Overwrite in place — the slot already holds a live object
				*pointer = std::move(component);
			}
			else
			{
				new (pointer) TComponent(std::move(component));
				markPresent(slot);
			}
			return pointer;
		}
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
		return componentPointerForSlot(entity.slot);
	}

	void removeComponent(Entity entity)
	{
		const uint32_t slot = entity.slot;
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (!_presence.test(slot)) [[unlikely]]
			return;
#endif

		if constexpr (STORES_DATA && !std::is_trivially_destructible_v<TComponent>)
		{
			componentPointerForSlot(slot)->~TComponent();
		}
		markAbsent(slot);
	}

	// Unchecked — caller guarantees the presence bit is set.
	[[nodiscard]] TComponent* componentPointerForSlot(uint32_t slot) noexcept
	{
		if constexpr (!STORES_DATA)
		{
			return sharedTagInstance();
		}
		else
		{
			return blockEntry(slot >> BLOCK_SHIFT).block.load(std::memory_order_acquire)->pointerTo(slot & BLOCK_MASK);
		}
	}

	// Enables the dense-run fast path in views
	static constexpr bool CONTIGUOUS_DATA = STORES_DATA;

	// Unchecked — only valid when the whole presence word is set; a word never straddles blocks
	[[nodiscard]] TComponent* componentRunPointer(uint32_t wordIndex) noexcept
	{
		const uint32_t slot = wordIndex << 6;
		return blockEntry(slot >> BLOCK_SHIFT).block.load(std::memory_order_acquire)->pointerTo(slot & BLOCK_MASK);
	}

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
		return _liveCount.load(std::memory_order_relaxed);
	}

	// Pre-allocates blocks and presence bits for slotCapacity slots.
	void reserve(size_t slotCapacity)
	{
		constexpr uint64_t maxSlotCapacity = uint64_t{1} << 32;
		if (slotCapacity > maxSlotCapacity)
		{
			throw std::length_error("ComponentColumn slot capacity exceeds index range");
		}
		_presence.reserveSlots(static_cast<uint32_t>(std::min<uint64_t>(slotCapacity, maxSlotCapacity - 1)));
		const uint32_t blocksNeeded = static_cast<uint32_t>(slotCapacity / BlockSize + (slotCapacity % BlockSize != 0));
		if constexpr (STORES_DATA)
		{
			for (uint32_t blockIndex = 0; blockIndex < blocksNeeded; ++blockIndex)
			{
				ensureBlock(blockIndex);
			}
		}
		else if (blocksNeeded != 0)
		{
			const uint32_t lastTable = std::bit_width(blocksNeeded - 1);
			for (uint32_t tableIndex = 0; tableIndex <= lastTable; ++tableIndex)
			{
				ensureBlockEntry(tableStart(tableIndex));
			}
		}
	}

	[[nodiscard]] StorageStatistics statistics() const noexcept
	{
		StorageStatistics stats;
		stats.liveComponentCount = _liveCount.load(std::memory_order_relaxed);
		stats.slotsPerBlock = BlockSize;
		if constexpr (STORES_DATA)
		{
			for (const auto& entry : _blocks)
			{
				BlockTable* table = entry.load(std::memory_order_acquire);
				if (!table) continue;
				for (uint32_t i = 0; i < table->size; ++i)
				{
					if (table->entries[i].block.load(std::memory_order_acquire)) ++stats.allocatedBlockCount;
				}
			}
			stats.allocatedComponentBytes =
			    static_cast<size_t>(stats.allocatedBlockCount) * BlockSize * sizeof(TComponent);
			stats.liveComponentBytes = static_cast<size_t>(stats.liveComponentCount) * sizeof(TComponent);
		}
		return stats;
	}
};

} // namespace Orhescyon
