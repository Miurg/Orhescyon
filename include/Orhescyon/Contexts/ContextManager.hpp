#pragma once

#include <typeinfo>
#include <iostream>
#include <unordered_map>

#include "../Entitys/Entity.hpp"

namespace Orhescyon
{
// Maps a context type (via typeid hash) -> Entity for singleton-like global resource access.
class ContextManager
{
private:
	std::unordered_map<size_t, Entity> _contexts;

public:
	template <typename TContext>
	void registerContext(Entity entity)
	{
		_contexts[typeid(TContext).hash_code()] = entity;
	}

	// Returns Entity::invalid() for a context that was never registered.
	template <typename TContext>
	Entity getContext()
	{
		auto it = _contexts.find(typeid(TContext).hash_code());
		if (it == _contexts.end())
		{
#ifdef ORHESCYON_HIGH_CHECK
			std::cerr << "WARNING::CONTEXT_MANAGER::Context " << typeid(TContext).name()
			          << " not registered, returning invalid entity" << std::endl;
#endif
			return Entity::invalid();
		}
		return it->second;
	}
};
}
