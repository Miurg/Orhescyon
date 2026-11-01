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
	// Iterates this system's subscribers, binding RequiredComponents to func.
	template <typename TFunc>
	void forEachSubscribedEntity(GeneralManager& gm, TFunc&& func)
	{
		gm.forEachSubscribedEntityWith<Derived, RequiredComponents...>(std::forward<TFunc>(func));
	}

	template <typename TFunc>
	void parallelForEachSubscribedEntity(GeneralManager& gm, TFunc&& func)
	{
		gm.parallelForEachSubscribedEntityWith<Derived, RequiredComponents...>(std::forward<TFunc>(func));
	}

public:
	~SystemCore() override = default;

	void update(GeneralManager& gm) override
	{
		// Optional: Override in derived class if needed
	}

	bool shouldProcessEntity(Entity entity, GeneralManager& gm) override
	{
		return (gm.hasComponent<RequiredComponents>(entity) && ...);
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

	void onEntitySubscribed(Entity, GeneralManager&) override {}

	void onEntityUnsubscribed(Entity entity, GeneralManager& gm) override
	{
		// Optional: Override in derived class if needed
		// TODO: create return variables
	}
};
} // namespace Orhescyon
