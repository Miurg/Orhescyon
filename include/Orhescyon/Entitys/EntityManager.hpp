#pragma once

#include <cstdint>
#include <atomic>
#include "ActiveEntitySet.hpp"
#include <iostream>

using Entity = uint32_t;

namespace Orhescyon
{
class EntityManager
{
private:
	std::atomic<Entity> _nextEntity;
	ActiveEntitySet _activeEntities;

public:
	EntityManager() : _nextEntity(1) {}

	Entity createEntity()
	{
		Entity entity = _nextEntity.fetch_add(1, std::memory_order_relaxed);
		_activeEntities.insert(entity);
		return entity;
	}

	void destroyEntity(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_activeEntities.contains(entity))
		{
			std::cerr << "WARNING::ENTITY_MANAGER::DestroyEntity on inactive entity " << entity << std::endl;
			return;
		}
#endif
		_activeEntities.erase(entity);
	}

	bool isActive(Entity entity) const noexcept
	{
		return _activeEntities.contains(entity);
	}

	const ActiveEntitySet& getActiveEntities() const noexcept
	{
		return _activeEntities;
	}
};

}
