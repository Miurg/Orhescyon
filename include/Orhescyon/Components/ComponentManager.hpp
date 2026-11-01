#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

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
	struct StorageEntry
	{
		std::shared_ptr<void> storage;
		std::function<void(Entity)> remove;
	};

	std::shared_mutex _registryMutex;
	std::unordered_map<std::type_index, StorageEntry> _componentStorages;

	template <typename TComponent>
	StorageFor<TComponent>& getOrCreateStorage()
	{
		auto typeIndex = std::type_index(typeid(TComponent));
		{
			std::shared_lock lock(_registryMutex);
			auto it = _componentStorages.find(typeIndex);
			if (it != _componentStorages.end())
			{
				return *std::static_pointer_cast<StorageFor<TComponent>>(it->second.storage);
			}
		}

		std::unique_lock lock(_registryMutex);
		auto it = _componentStorages.find(typeIndex);
		if (it != _componentStorages.end())
		{
			return *std::static_pointer_cast<StorageFor<TComponent>>(it->second.storage);
		}

		auto storage = std::make_shared<StorageFor<TComponent>>();
		_componentStorages.emplace(typeIndex, StorageEntry{storage, [storage](Entity entity)
		                                                  { storage->removeComponent(entity); }});
		return *storage;
	}

public:
	ComponentManager()
	{
		// first lock can race in mingw, so do it here before threads use it
		std::unique_lock lock(_registryMutex);
	}

	template <typename TComponent, typename... Args>
	TComponent* addComponent(Entity entity, Args&&... args)
	{
		return getOrCreateStorage<TComponent>().addComponent(entity, TComponent{std::forward<Args>(args)...});
	}

	template <typename TComponent>
	bool hasComponent(Entity entity)
	{
		StorageFor<TComponent>* storage;
		{
			std::shared_lock lock(_registryMutex);
			auto it = _componentStorages.find(std::type_index(typeid(TComponent)));
			if (it == _componentStorages.end()) return false;
			storage = static_cast<StorageFor<TComponent>*>(it->second.storage.get());
		}
		return storage->hasComponent(entity);
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
		std::vector<std::function<void(Entity)>> callbacks;
		{
			std::shared_lock lock(_registryMutex);
			callbacks.reserve(_componentStorages.size());
			for (const auto& [type, entry] : _componentStorages)
			{
				callbacks.push_back(entry.remove);
			}
		}
		for (auto& callback : callbacks)
		{
			callback(entity);
		}
	}
};

} // namespace Orhescyon
