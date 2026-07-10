#pragma once

#include <iostream>

#include "ISystemCore.hpp"
#include "../GeneralManager.hpp"

namespace Orhescyon
{
// CRTP base for systems. RequiredComponents are auto-checked in shouldProcessEntity().
template <typename Derived, typename... RequiredComponents>
class SystemCore : public ISystemCore
{
protected:
	template <typename... Comps>
	static bool hasAllComponents(Entity entity, GeneralManager& gm, const char* systemName)
	{
		return (checkComponent<Comps>(entity, gm, systemName) && ...);
	}
	template <typename T>
	static bool checkComponent(Entity entity, GeneralManager& gm, const char* systemName)
	{
		if (gm.getComponent<T>(entity) == nullptr)
		{
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
			std::cerr << "WARNING::SYSTEM::Entity " << entity << " should not be processed by " << systemName
			          << " because it doesn't have required component: " << typeid(T).name() << std::endl;
#endif
			return false;
		}
		return true;
	}

	// Iterates this system's subscribers, binding RequiredComponents to func.
	template <typename TFunc>
	void forEachSubscribedEntity(GeneralManager& gm, TFunc&& func)
	{
		gm.forEachSubscribedEntityWith<Derived, RequiredComponents...>(std::forward<TFunc>(func));
	}

public:
	~SystemCore() override = default;

	void update(GeneralManager& gm) override
	{
		// Optional: Override in derived class if needed
	}

	bool shouldProcessEntity(Entity entity, GeneralManager& gm) override
	{
		return hasAllComponents<RequiredComponents...>(entity, gm, typeid(Derived).name());
	}

	[[nodiscard]] size_t requiredComponentCount() const override
	{
		return sizeof...(RequiredComponents);
	}

	void onRegistered(GeneralManager& gm) override
	{
		// Optional: Override in derived class if needed
		// TODO: create return variables
	}

	void onShutdown(GeneralManager& gm) override
	{
		// Optional: Override in derived class if needed
		// TODO: create return variables
	}

	void onEntitySubscribed(Entity entity, GeneralManager& gm) override
	{
		if (!shouldProcessEntity(entity, gm))
		{
			return; // TODO: create return variables
		}
	}

	void onEntityUnsubscribed(Entity entity, GeneralManager& gm) override
	{
		// Optional: Override in derived class if needed
		// TODO: create return variables
	}
};
} // namespace Orhescyon
