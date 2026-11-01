#pragma once

#include <type_traits>
#include <typeindex>

#include "../Components/ComponentManager.hpp"
#include "SystemManager.hpp"
#include "SystemSchedulingMetadata.hpp"

namespace Orhescyon
{
// Fluent chain returned by registerSystem.
template <typename TSystem>
class SystemRegistration
{
	SystemManager* _systemManager;
	ComponentManager& _componentManager;

	SystemSchedulingMetadata* metadata()
	{
		return &_systemManager->schedulingMetadataFor(typeid(TSystem));
	}

public:
	explicit SystemRegistration(SystemManager* systemManager, ComponentManager& componentManager)
	    : _systemManager(systemManager), _componentManager(componentManager)
	{
	}

	template <typename... TSystems>
	SystemRegistration& before()
	{
		(metadata()->beforeSystems.emplace_back(typeid(TSystems)), ...);
		_systemManager->markExecutionOrderDirty();
		return *this;
	}

	template <typename... TSystems>
	SystemRegistration& after()
	{
		(metadata()->afterSystems.emplace_back(typeid(TSystems)), ...);
		_systemManager->markExecutionOrderDirty();
		return *this;
	}

	template <typename... TComponents>
	SystemRegistration& reads()
	{
		(_componentManager.getStorage<std::remove_const_t<TComponents>>(), ...);
		(metadata()->readComponents.emplace_back(typeid(TComponents)), ...);
		_systemManager->markExecutionOrderDirty();
		return *this;
	}

	template <typename... TComponents>
	SystemRegistration& writes()
	{
		(_componentManager.getStorage<std::remove_const_t<TComponents>>(), ...);
		(metadata()->writeComponents.emplace_back(typeid(TComponents)), ...);
		_systemManager->markExecutionOrderDirty();
		return *this;
	}
};
} // namespace Orhescyon
