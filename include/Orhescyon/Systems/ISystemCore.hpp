#pragma once

#include <cstddef>
#include <string_view>
#include "../Entitys/Entity.hpp"

namespace Orhescyon
{
class GeneralManager;
// System interface. Lifecycle: onRegistered → (onEntitySubscribed/update/onEntityUnsubscribed)* → onShutdown.
class ISystemCore
{
public:
	virtual ~ISystemCore() = default;
	virtual void update(GeneralManager& gm) = 0;
	virtual bool shouldProcessEntity(Entity entity, GeneralManager& gm) = 0;
	virtual void onRegistered(GeneralManager& gm) = 0;
	virtual void onShutdown(GeneralManager& gm) = 0;
	virtual void onEntitySubscribed(Entity entity, GeneralManager& gm) = 0;
	virtual void onEntityUnsubscribed(Entity entity, GeneralManager& gm) = 0;

	// Scheduling diagnostics only
	[[nodiscard]] virtual size_t requiredComponentCount() const
	{
		return 0;
	}

	[[nodiscard]] virtual std::string_view getSystemManagerName() const
	{
		return "default";
	}
};
} // namespace Orhescyon
