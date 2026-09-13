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
#ifdef ORHESCYON_HIGH_CHECK
		if (!_systemManager) return nullptr;
#endif
		return &_systemManager->schedulingMetadataFor(typeid(TSystem));
	}

public:
	explicit SystemRegistration(SystemManager* systemManager, ComponentManager& componentManager)
	    : _systemManager(systemManager), _componentManager(componentManager) {}

	template <typename... TSystems>
	SystemRegistration& before()
	{
		if (SystemSchedulingMetadata* target = metadata())
		{
			(target->beforeSystems.emplace_back(typeid(TSystems)), ...);
			_systemManager->markExecutionOrderDirty();
		}
		return *this;
	}

	template <typename... TSystems>
	SystemRegistration& after()
	{
		if (SystemSchedulingMetadata* target = metadata())
		{
			(target->afterSystems.emplace_back(typeid(TSystems)), ...);
			_systemManager->markExecutionOrderDirty();
		}
		return *this;
	}

	template <typename... TComponents>
	SystemRegistration& reads()
	{
		if (SystemSchedulingMetadata* target = metadata())
		{
			(_componentManager.getStorage<std::remove_const_t<TComponents>>(), ...);
			(target->readComponents.emplace_back(typeid(TComponents)), ...);
			_systemManager->markExecutionOrderDirty();
		}
		return *this;
	}

	template <typename... TComponents>
	SystemRegistration& writes()
	{
		if (SystemSchedulingMetadata* target = metadata())
		{
			(_componentManager.getStorage<std::remove_const_t<TComponents>>(), ...);
			(target->writeComponents.emplace_back(typeid(TComponents)), ...);
			_systemManager->markExecutionOrderDirty();
		}
		return *this;
	}
};
}
