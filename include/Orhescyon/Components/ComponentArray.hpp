#pragma once
#include "StablePool.hpp"
#include "../Entitys/Entity.hpp"
#include <limits>
#include <vector>

namespace Orhescyon
{
// Sparse array mapping Entity -> slot in a StablePool.
// Adding a component to an entity that already has one overwrites it in-place.
template <typename TComponent, uint32_t PoolBlockSize = 4096>
class ComponentArray
{
private:
	StablePool<TComponent, PoolBlockSize> _pool;

	std::vector<uint32_t> _sparse;
	static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();

public:
	TComponent* addComponent(Entity entity, TComponent&& component)
	{
		if (entity.slot >= _sparse.size())
		{
			_sparse.resize(entity.slot * 2 + 1, INVALID_INDEX);
		}
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (_sparse[entity.slot] != INVALID_INDEX)
		{
			uint32_t index = _sparse[entity.slot];
			_pool[index] = std::move(component);
			return _pool.at(index);
		}
#endif

		auto [newIndex, ptr] = _pool.allocate(std::move(component));

		_sparse[entity.slot] = newIndex;
		return ptr;
	}

	[[nodiscard]] bool hasComponent(Entity entity) const noexcept
	{
		if (entity.slot >= _sparse.size()) return false;
		return _sparse[entity.slot] != INVALID_INDEX;
	}

	TComponent* getComponent(Entity entity)
	{
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (entity.slot >= _sparse.size()) [[unlikely]]
		{
			return nullptr;
		}
#endif
		uint32_t index = _sparse[entity.slot];

#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (index == INVALID_INDEX) [[unlikely]]
		{
			return nullptr;
		}
#endif

		return _pool.at(index);
	}

	void removeComponent(Entity entity)
	{
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (entity.slot >= _sparse.size()) [[unlikely]]
			return;
#endif
		uint32_t indexToRemove = _sparse[entity.slot];
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (indexToRemove == INVALID_INDEX) [[unlikely]]
			return;
#endif
		_pool.deallocate(indexToRemove);

		_sparse[entity.slot] = INVALID_INDEX;
	}

	[[nodiscard]] size_t size() const
	{
		return _pool.liveCount();
	}

	void reserve(size_t capacity)
	{
		uint32_t numBlocks = static_cast<uint32_t>((capacity + PoolBlockSize - 1) / PoolBlockSize);
		_pool.reserveBlocks(numBlocks);
	}
};
}
