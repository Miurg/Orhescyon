#pragma once

#include <typeinfo>
#include <iostream>

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

	template <typename TContext>
	Entity getContext()
	{
#ifdef ORHESCYON_HIGH_CHECK
		size_t hash = typeid(TContext).hash_code();
		if (!_contexts.contains(hash))
		{
			std::cerr << "WARNING::CONTEXT_MANAGER::Context " << typeid(TContext).name()
			          << " not registered, returning 0" << std::endl;
			return 0;
		}
#endif
		return _contexts[typeid(TContext).hash_code()];
	}
};
}
