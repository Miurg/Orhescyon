#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

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

	std::vector<std::unique_ptr<Block>> _blocks; // indexed by slot >> BLOCK_SHIFT, entries may be null
	SlotBitmap _presence;
	std::vector<uint32_t> _blockLiveCounts;
	uint32_t _liveCount = 0;

	// All tag instances are interchangeable, so every slot shares one address
	static TComponent* sharedTagInstance() noexcept
	{
		static TComponent instance{};
		return &instance;
	}

	// ensure that block by index exist and if not - create that block
	Block& ensureBlock(uint32_t blockIndex)
	{
		if (blockIndex >= _blocks.size())
		{
			_blocks.resize(std::max<size_t>(blockIndex + 1, _blocks.size() * 2));
		}
		if (!_blocks[blockIndex])
		{
			_blocks[blockIndex] = std::make_unique<Block>();
		}
		return *_blocks[blockIndex];
	}

	void markPresent(uint32_t slot)
	{
		_presence.set(slot);
		const uint32_t blockIndex = slot >> BLOCK_SHIFT;
		if (blockIndex >= _blockLiveCounts.size())
		{
			_blockLiveCounts.resize(std::max<size_t>(blockIndex + 1, _blockLiveCounts.size() * 2), 0);
		}
		++_blockLiveCounts[blockIndex];
		++_liveCount;
	}

	void markAbsent(uint32_t slot) noexcept
	{
		_presence.clear(slot);
		--_blockLiveCounts[slot >> BLOCK_SHIFT];
		--_liveCount;
	}

public:
	ComponentColumn() = default;

	~ComponentColumn()
	{
		if constexpr (STORES_DATA && !std::is_trivially_destructible_v<TComponent>)
		{
			_presence.forEachSetBit([this](uint32_t slot)
			                        { _blocks[slot >> BLOCK_SHIFT]->pointerTo(slot & BLOCK_MASK)->~TComponent(); });
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
			_blocks[slot >> BLOCK_SHIFT]->pointerTo(slot & BLOCK_MASK)->~TComponent();
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
			return _blocks[slot >> BLOCK_SHIFT]->pointerTo(slot & BLOCK_MASK);
		}
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
		return _liveCount;
	}

	// Pre-allocates blocks and presence bits for slotCapacity slots.
	void reserve(size_t slotCapacity)
	{
		_presence.reserveSlots(static_cast<uint32_t>(slotCapacity));
		const size_t blocksNeeded = (slotCapacity + BlockSize - 1) / BlockSize;
		if (blocksNeeded > _blockLiveCounts.size())
		{
			_blockLiveCounts.resize(blocksNeeded, 0);
		}
		if constexpr (STORES_DATA)
		{
			if (blocksNeeded > _blocks.size())
			{
				_blocks.resize(blocksNeeded);
			}
			for (size_t blockIndex = 0; blockIndex < blocksNeeded; ++blockIndex)
			{
				if (!_blocks[blockIndex])
				{
					_blocks[blockIndex] = std::make_unique<Block>();
				}
			}
		}
	}

	[[nodiscard]] StorageStatistics statistics() const noexcept
	{
		StorageStatistics stats;
		stats.liveComponentCount = _liveCount;
		stats.slotsPerBlock = BlockSize;
		if constexpr (STORES_DATA)
		{
			for (const auto& block : _blocks)
			{
				if (block) ++stats.allocatedBlockCount;
			}
			stats.allocatedComponentBytes =
			    static_cast<size_t>(stats.allocatedBlockCount) * BlockSize * sizeof(TComponent);
			stats.liveComponentBytes = static_cast<size_t>(_liveCount) * sizeof(TComponent);
		}
		return stats;
	}
};

} // namespace Orhescyon
