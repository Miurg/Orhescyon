#pragma once
#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

#include "../Entitys/Entity.hpp"
#include "ComponentColumn.hpp"
#include "ComponentStorageTraits.hpp"
#include "SparseComponentStorage.hpp"

namespace Orhescyon
{
// Type-erased registry of component storages. 
// ComponentColumn by default,
// SparseComponentStorage as the opt-in for big rare components.
class ComponentManager
{
public:
	template <typename TComponent>
	using StorageFor =
	    std::conditional_t<ComponentStorageTraits<TComponent>::policy == StoragePolicy::Column,
	                       ComponentColumn<TComponent, ComponentStorageTraits<TComponent>::blockSize>,
	                       SparseComponentStorage<TComponent, ComponentStorageTraits<TComponent>::blockSize>>;

private:
	std::unordered_map<std::type_index, std::shared_ptr<void>> _componentStorages;
	std::unordered_map<std::type_index, std::function<void(Entity)>> _removeCallbacks;

	template <typename TComponent>
	StorageFor<TComponent>& getOrCreateStorage()
	{
		auto typeIndex = std::type_index(typeid(TComponent));
		if (!_componentStorages.contains(typeIndex))
		{
			_componentStorages[typeIndex] = std::make_shared<StorageFor<TComponent>>();
			_removeCallbacks[typeIndex] = [this](Entity entity)
			{
				getOrCreateStorage<TComponent>().removeComponent(entity);
			};
		}
		return *std::static_pointer_cast<StorageFor<TComponent>>(_componentStorages[typeIndex]);
	}

public:
	template <typename TComponent, typename... Args>
	TComponent* addComponent(Entity entity, Args&&... args)
	{
		return getOrCreateStorage<TComponent>().addComponent(entity, TComponent{std::forward<Args>(args)...});
	}

	template <typename TComponent>
	bool hasComponent(Entity entity)
	{
		auto it = _componentStorages.find(std::type_index(typeid(TComponent)));
		if (it == _componentStorages.end()) return false;
		return std::static_pointer_cast<StorageFor<TComponent>>(it->second)->hasComponent(entity);
	}

	template <typename TComponent>
	TComponent* getComponent(Entity entity)
	{
		return getOrCreateStorage<TComponent>().getComponent(entity);
	}

	template <typename TComponent>
	void removeComponent(Entity entity)
	{
		getOrCreateStorage<TComponent>().removeComponent(entity);
	}

	template <typename TComponent>
	StorageFor<TComponent>& getStorage()
	{
		return getOrCreateStorage<TComponent>();
	}

	void removeEntity(Entity entity)
	{
		for (auto& [type, callback] : _removeCallbacks)
		{
			callback(entity);
		}
	}
};

} // namespace Orhescyon
