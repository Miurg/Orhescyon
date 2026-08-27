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
		command.apply(gm);
	}
}

inline DeferredEntity DeferredChangeQueue::createEntity(GeneralManager& gm, std::string_view smName)
{
#if defined(ORHESCYON_HIGH_CHECK)
	if (!gm.hasSystemManager(smName))
	{
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::CreateEntity: SystemManager \"" << smName
		          << "\" not found. Command ignored." << std::endl;
		return {};
	}
#endif

	std::scoped_lock lock(_mutex);
	const uint64_t tokenId = _nextDeferredEntityId++;
	deferredQueue(smName).push_back(
	    DeferredCommand{DeferredOpKind::CreateEntity, [this, tokenId](GeneralManager& gm)
	                    { _deferredEntities.emplace(tokenId, gm.createEntityImmediate()); }});
	return {tokenId};
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
	                                                { gm.destroyEntityImmediate(entity); }});
}

inline void DeferredChangeQueue::destroyEntity(GeneralManager& gm, DeferredEntity handle, std::string_view smName)
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
	const uint64_t tokenId = handle.id;
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::DestroyEntity, [this, tokenId](GeneralManager& gm)
	                                                {
		                                                Entity target = resolveDeferredToken(tokenId, "DestroyEntity");
		                                                if (target == Entity::invalid()) return;
		                                                gm.destroyEntityImmediate(target);
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
		std::apply([entity, &gm](auto&&... unpacked)
		           { gm.addComponentImmediate<TComponent>(entity, std::move(unpacked)...); }, std::move(componentArgs));
	};
	auto task = std::make_shared<decltype(lambda)>(std::move(lambda));
	std::scoped_lock lock(_mutex);
	deferredQueue(smName).push_back(
	    DeferredCommand{DeferredOpKind::AddComponent, [task](GeneralManager& gm) { (*task)(gm); }});
}

template <typename TComponent, typename... Args>
void DeferredChangeQueue::addComponent(GeneralManager& gm, DeferredEntity handle, Args&&... args)
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

	const uint64_t tokenId = handle.id;
	auto lambda = [this, tokenId, componentArgs = tupleWithoutLast(std::move(ownedArgs))](GeneralManager& gm) mutable
	{
		Entity target = resolveDeferredToken(tokenId, "AddComponent");
		if (target == Entity::invalid()) return;
		std::apply([target, &gm](auto&&... unpacked)
		           { gm.addComponentImmediate<TComponent>(target, std::move(unpacked)...); }, std::move(componentArgs));
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
	                                                { gm.removeComponentImmediate<TComponent>(entity); }});
}

template <typename TComponent>
void DeferredChangeQueue::removeComponent(GeneralManager& gm, DeferredEntity handle, std::string_view smName)
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
	const uint64_t tokenId = handle.id;
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::RemoveComponent, [this, tokenId](GeneralManager& gm)
	                                                {
		                                                Entity target = resolveDeferredToken(tokenId, "RemoveComponent");
		                                                if (target == Entity::invalid()) return;
		                                                gm.removeComponentImmediate<TComponent>(target);
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
	                                                { gm.subscribeEntityImmediate<TSystem>(entity); }});
}

template <typename TSystem>
void DeferredChangeQueue::subscribeEntity(GeneralManager& gm, DeferredEntity handle, std::string_view smName)
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
	const uint64_t tokenId = handle.id;
	deferredQueue(smName).push_back(DeferredCommand{DeferredOpKind::SubscribeEntity, [this, tokenId](GeneralManager& gm)
	                                                {
		                                                Entity target = resolveDeferredToken(tokenId, "SubscribeEntity");
		                                                if (target == Entity::invalid()) return;
		                                                gm.subscribeEntityImmediate<TSystem>(target);
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
	                                                { gm.unsubscribeEntityImmediate<TSystem>(entity); }});
}

template <typename TSystem>
void DeferredChangeQueue::unsubscribeEntity(GeneralManager& gm, DeferredEntity handle, std::string_view smName)
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
	const uint64_t tokenId = handle.id;
	deferredQueue(smName).push_back(
	    DeferredCommand{DeferredOpKind::UnsubscribeEntity, [this, tokenId](GeneralManager& gm)
	                    {
		                    Entity target = resolveDeferredToken(tokenId, "UnsubscribeEntity");
		                    if (target == Entity::invalid()) return;
		                    gm.unsubscribeEntityImmediate<TSystem>(target);
	                    }});
}

} // namespace Orhescyon
