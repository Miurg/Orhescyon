#pragma once

#include <limits>
#include <vector>

#include "Entity.hpp"

namespace Orhescyon
{
// Sparse/dense set — O(1) insert, erase (swap-with-last), and contains.
class ActiveEntitySet
{
public:
	ActiveEntitySet() = default;

	[[nodiscard]] bool contains(Entity entity) const noexcept
	{
		if (entity.slot >= _sparse.size()) return false;
		size_t idx = _sparse[entity.slot];
		// Full compare — a recycled slot with another generation is a different entity
		return idx < _dense.size() && _dense[idx] == entity;
	}

	void insert(Entity entity)
	{
		if (contains(entity)) return;
		if (entity.slot >= _sparse.size()) _sparse.resize(entity.slot + 1, INVALID_INDEX);
		_sparse[entity.slot] = _dense.size();
		_dense.push_back(entity);
	}

	void erase(Entity entity)
	{
		if (!contains(entity)) return;
		size_t idx = _sparse[entity.slot];
		Entity last = _dense.back();
		_dense[idx] = last;
		_sparse[last.slot] = idx;
		_dense.pop_back();
		_sparse[entity.slot] = INVALID_INDEX;
	}

	void clear() noexcept
	{
		_dense.clear();
		_sparse.clear();
	}

	[[nodiscard]] size_t size() const noexcept
	{
		return _dense.size();
	}

	[[nodiscard]] const std::vector<Entity>& dense() const noexcept
	{
		return _dense;
	}

	[[nodiscard]] auto begin() const noexcept
	{
		return _dense.begin();
	}
	[[nodiscard]] auto end() const noexcept
	{
		return _dense.end();
	}

private:
	static constexpr size_t INVALID_INDEX = std::numeric_limits<size_t>::max();

	std::vector<Entity> _dense;
	std::vector<size_t> _sparse;
};

}
