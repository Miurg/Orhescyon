#pragma once

#include "DeferredChangeQueue.hpp"
#include "../GeneralManager.hpp"

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
#if defined(ORHESCYON_HIGH_CHECK)
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::DestroyEntity: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		return;
	}
#endif
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::DestroyEntity, [entity](GeneralManager& gm)
	                                                {
		                                                if (!gm.isActive(entity)) return;
		                                                gm.destroyEntityImmediate(entity);
	                                                }});
}

template <typename TComponent, typename... Args>
void DeferredChangeQueue::addComponent(GeneralManager& gm, Entity entity, Args&&... args)
{
	static_assert(sizeof...(Args) >= 1, "addComponent requires the SystemManager name as its last argument");

	using ArgsTuple = std::tuple<std::decay_t<Args>...>;
	ArgsTuple ownedArgs(std::forward<Args>(args)...);
	std::string smName(std::get<sizeof...(Args) - 1>(ownedArgs));

#if defined(ORHESCYON_HIGH_CHECK)
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::AddComponent: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		return;
	}
#endif
	// all this shit for move-only args. same as jobpool
	// TODO: do better
	auto lambda = [entity, componentArgs = tupleWithoutLast(std::move(ownedArgs))](GeneralManager& gm) mutable
	{
		if (!gm.isActive(entity)) return;
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
#if defined(ORHESCYON_HIGH_CHECK)
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::RemoveComponent: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		return;
	}
#endif
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::RemoveComponent, [entity](GeneralManager& gm)
	                                                {
		                                                if (!gm.isActive(entity)) return;
		                                                gm.removeComponentImmediate<TComponent>(entity);
	                                                }});
}

template <typename TSystem>
void DeferredChangeQueue::subscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName)
{
#if defined(ORHESCYON_HIGH_CHECK)
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::SubscribeEntity: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		return;
	}
#endif
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::SubscribeEntity, [entity](GeneralManager& gm)
	                                                {
		                                                if (!gm.isActive(entity)) return;
		                                                gm.subscribeEntityImmediate<TSystem>(entity);
	                                                }});
}

template <typename TSystem>
void DeferredChangeQueue::unsubscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName)
{
#if defined(ORHESCYON_HIGH_CHECK)
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::UnsubscribeEntity: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		return;
	}
#endif
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::UnsubscribeEntity, [entity](GeneralManager& gm)
	                                                {
		                                                if (!gm.isActive(entity)) return;
		                                                gm.unsubscribeEntityImmediate<TSystem>(entity);
	                                                }});
}

} // namespace Orhescyon
