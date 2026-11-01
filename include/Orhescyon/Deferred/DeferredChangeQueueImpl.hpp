#pragma once

#include "DeferredChangeQueue.hpp"
#include "../GeneralManager.hpp"
#include <cstdlib>

namespace Orhescyon
{

inline void DeferredChangeQueue::flushDeferred(GeneralManager& gm, std::string_view smName)
{
	std::unique_lock<std::mutex> lock(_mutex);
	auto queueIt = _deferredQueues.find(std::string(smName));
	if (queueIt == _deferredQueues.end() || queueIt->second.empty()) return;

	std::vector<DeferredCommand> batch;

	batch.swap(queueIt->second);
	lock.unlock();

	for (DeferredCommand& command : batch)
	{
		command.execute(gm);
	}
}

inline void DeferredChangeQueue::destroyEntity(GeneralManager& gm, Entity entity, std::string_view smName)
{
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::DestroyEntity: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		std::abort();
	}
	if (!gm.isActive(entity))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::DestroyEntity on inactive entity " << entity << std::endl;
		return;
	}
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::DestroyEntity, [entity](GeneralManager& gm)
	                                                { gm.destroyEntityImmediate(entity); }});
}

template <typename TComponent, typename... Args>
void DeferredChangeQueue::addComponent(GeneralManager& gm, Entity entity, Args&&... args)
{
	static_assert(sizeof...(Args) >= 1, "addComponent requires the SystemManager name as its last argument");

	auto argsRefs = std::tie(args...);
	std::string smName(std::get<sizeof...(Args) - 1>(argsRefs));

	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::AddComponent: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		std::abort();
	}
	if (!gm.isActive(entity))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::AddComponent on inactive entity " << entity << std::endl;
		return;
	}

	using ArgsTuple = std::tuple<std::decay_t<Args>...>;
	ArgsTuple ownedArgs(std::forward<Args>(args)...);

	// all this shit for move-only args. same as jobpool
	// TODO: do better
	auto lambda = [entity, componentArgs = tupleWithoutLast(std::move(ownedArgs))](GeneralManager& gm) mutable
	{
		std::apply([entity, &gm](auto&&... unpacked)
		           { gm.addComponentImmediate<TComponent>(entity, std::move(unpacked)...); }, std::move(componentArgs));
	};
	auto task = std::make_shared<decltype(lambda)>(std::move(lambda));
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(
	    DeferredCommand{DeferredOpKind::AddComponent, [task](GeneralManager& gm) { (*task)(gm); }});
}

template <typename TComponent>
void DeferredChangeQueue::removeComponent(GeneralManager& gm, Entity entity, std::string_view smName)
{
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::RemoveComponent: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		std::abort();
	}
	if (!gm.isActive(entity))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::RemoveComponent on inactive entity " << entity << std::endl;
		return;
	}
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::RemoveComponent, [entity](GeneralManager& gm)
	                                                { gm.removeComponentImmediate<TComponent>(entity); }});
}

template <typename TSystem>
void DeferredChangeQueue::subscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName)
{
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::SubscribeEntity: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		std::abort();
	}
	if (!gm.isActive(entity))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::SubscribeEntity on inactive entity " << entity << std::endl;
		return;
	}
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::SubscribeEntity, [entity](GeneralManager& gm)
	                                                { gm.subscribeEntityImmediate<TSystem>(entity); }});
}

template <typename TSystem>
void DeferredChangeQueue::unsubscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName)
{
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::UnsubscribeEntity: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		std::abort();
	}
	if (!gm.isActive(entity))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::UnsubscribeEntity on inactive entity " << entity << std::endl;
		return;
	}
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::UnsubscribeEntity, [entity](GeneralManager& gm)
	                                                { gm.unsubscribeEntityImmediate<TSystem>(entity); }});
}

} // namespace Orhescyon
