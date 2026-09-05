#pragma once

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "Entity.hpp"
#include "SlotBitmap.hpp"

namespace Orhescyon
{
// Allocates entity slots and recycles them through a free list.
class EntityManager
{
private:
	std::vector<uint32_t> _generations;
	std::vector<uint32_t> _freeSlots;
	SlotBitmap _aliveSlots;
	mutable std::shared_mutex _mutex;

	[[nodiscard]] bool isActiveUnlocked(Entity entity) const noexcept
	{
		return _aliveSlots.test(entity.slot) && _generations[entity.slot] == entity.generation;
	}

public:
	Entity createEntity()
	{
		std::unique_lock lock(_mutex);

		uint32_t slot;
		if (!_freeSlots.empty())
		{
			slot = _freeSlots.back();
			_freeSlots.pop_back();
		}
		else
		{
			slot = static_cast<uint32_t>(_generations.size());
			_aliveSlots.reserveSlots(slot + 1);
			_generations.push_back(0);
		}
		_aliveSlots.set(slot);
		return {slot, _generations[slot]};
	}

	void destroyEntity(Entity entity)
	{
		std::unique_lock lock(_mutex);
		if (!isActiveUnlocked(entity)) [[unlikely]] return;

		_freeSlots.push_back(entity.slot);
		++_generations[entity.slot];
		_aliveSlots.clear(entity.slot);
	}

	[[nodiscard]] bool isActive(Entity entity) const noexcept
	{
		std::shared_lock lock(_mutex);
		return isActiveUnlocked(entity);
	}

	// Unchecked — view internals rebuild handles from live slots
	[[nodiscard]] uint32_t generationOfSlot(uint32_t slot) const noexcept
	{
		std::shared_lock lock(_mutex);
		return _generations[slot];
	}

	// Upper bound of slots ever allocated
	[[nodiscard]] uint32_t slotCapacity() const noexcept
	{
		std::shared_lock lock(_mutex);
		return static_cast<uint32_t>(_generations.size());
	}

	[[nodiscard]] uint32_t activeEntityCount() const noexcept
	{
		std::shared_lock lock(_mutex);
		return _aliveSlots.setBitCount();
	}

	template <typename TFunc>
	void forEachActiveEntity(TFunc&& func) const
	{
		std::vector<Entity> snapshot;
		{
			std::shared_lock lock(_mutex);
			snapshot.reserve(_aliveSlots.setBitCount());
			_aliveSlots.forEachSetBit(
			    [&](uint32_t slot) { snapshot.push_back(Entity{slot, _generations[slot]}); });
		}

		for (Entity entity : snapshot) func(entity);
	}
};

}
