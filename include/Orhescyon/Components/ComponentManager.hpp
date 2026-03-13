#pragma once
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "../Entitys/EntityManager.hpp"
#include "ComponentArray.hpp"

namespace Orhescyon
{
// Type-erased component storage. Each component type gets a ComponentArray.
// removeEntity() uses registered callbacks to erase from all arrays without knowing concrete types.
class ComponentManager
{
private:
	std::unordered_map<std::type_index, std::shared_ptr<void>> _componentArrays;
	std::unordered_map<std::type_index, std::function<void(Entity)>> _removeCallbacks;

	template <typename TComponent>
	ComponentArray<TComponent>& getComponentArray()
	{
		auto typeIndex = std::type_index(typeid(TComponent));
		if (_componentArrays.find(typeIndex) == _componentArrays.end())
		{
			_componentArrays[typeIndex] = std::make_shared<ComponentArray<TComponent>>();
			_removeCallbacks[typeIndex] = [this](Entity entity)
			{
				getComponentArray<TComponent>().removeComponent(entity);
			};
		}
		return *std::static_pointer_cast<ComponentArray<TComponent>>(_componentArrays[typeIndex]);
	}

public:
	template <typename TComponent, typename... Args>
	TComponent* addComponent(Entity entity, Args&&... args)
	{
		return getComponentArray<TComponent>().addComponent(entity, TComponent{std::forward<Args>(args)...});
	}

	template <typename TComponent>
	TComponent* getComponent(Entity entity)
	{
		return getComponentArray<TComponent>().getComponent(entity);
	}

	template <typename TComponent>
	void removeComponent(Entity entity)
	{
		getComponentArray<TComponent>().removeComponent(entity);
	}

	template <typename TComponent>
	ComponentArray<TComponent>& getAllComponents()
	{
		return getComponentArray<TComponent>();
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
