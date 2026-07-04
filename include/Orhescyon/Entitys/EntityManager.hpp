#pragma once

#include <cstdint>
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

public:
	Entity createEntity()
	{
		uint32_t slot;
		if (!_freeSlots.empty())
		{
			slot = _freeSlots.back();
			_freeSlots.pop_back();
		}
		else
		{
			slot = static_cast<uint32_t>(_generations.size());
			_generations.push_back(0);
		}
		_aliveSlots.set(slot);
		return {slot, _generations[slot]};
	}

	// Stale or double destroys are rejected
	void destroyEntity(Entity entity)
	{
		if (!isActive(entity)) return;

		++_generations[entity.slot];
		_aliveSlots.clear(entity.slot);
		_freeSlots.push_back(entity.slot);
	}

	[[nodiscard]] bool isActive(Entity entity) const noexcept
	{
		// A set alive bit guarantees the slot is within _generations
		return _aliveSlots.test(entity.slot) && _generations[entity.slot] == entity.generation;
	}

	// Upper bound of slots ever allocated
	[[nodiscard]] uint32_t slotCapacity() const noexcept
	{
		return static_cast<uint32_t>(_generations.size());
	}

	[[nodiscard]] uint32_t activeEntityCount() const noexcept
	{
		return _aliveSlots.setBitCount();
	}

	// Calls func(Entity) for every live entity in ascending slot order.
	template <typename TFunc>
	void forEachActiveEntity(TFunc&& func) const
	{
		_aliveSlots.forEachSetBit([&](uint32_t slot) { func(Entity{slot, _generations[slot]}); });
	}
};

}
