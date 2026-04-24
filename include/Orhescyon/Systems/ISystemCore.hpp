#pragma once

#include <string_view>
#include <vector>
#include <typeindex>
#include "../Entitys/ActiveEntitySet.hpp"

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
	virtual std::vector<std::type_index> getReadComponents() = 0;
	virtual std::vector<std::type_index> getWriteComponents() = 0;
	virtual std::vector<std::type_index> getBeforeSystems() = 0;
	virtual std::vector<std::type_index> getAfterSystems() = 0;
	virtual std::vector<std::type_index> getSystemDependencies() = 0;
	virtual void onEntitySubscribed(Entity entity, GeneralManager& gm) = 0;
	virtual void onEntityUnsubscribed(Entity entity, GeneralManager& gm) = 0;
	virtual std::string_view getSystemManagerName() const { return "default"; }
};
}