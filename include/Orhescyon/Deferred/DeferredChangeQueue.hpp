#pragma once

// Storage side of deferred structural changes: named per-SystemManager FIFO queues,
// a token registry for entities created inside batches, and single-pass flushing.

#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <mutex>
#include <memory>

#include "../Entitys/Entity.hpp"

namespace Orhescyon
{
class GeneralManager;

// Handle to an entity whose slot is allocated only when a deferred queue is applied.
// id 0 is the null handle.
// Real ids are handed out at record time and resolve through DeferredChangeQueue::resolveEntity once the batch owning
// the creation has been flushed.
struct DeferredEntity
{
	uint64_t id = 0;

	[[nodiscard]] constexpr bool operator==(const DeferredEntity&) const noexcept = default;
};

class DeferredChangeQueue
{
private:
	enum class DeferredOpKind : uint8_t
	{
		CreateEntity,
		DestroyEntity,
		AddComponent,
		RemoveComponent,
		SubscribeEntity,
		UnsubscribeEntity
	};

	// One queued structural change.
	struct DeferredCommand
	{
		DeferredOpKind kind;
		std::function<void(GeneralManager&)> apply;
	};

	std::unordered_map<std::string, std::vector<DeferredCommand>> _deferredQueues;
	// Kept for the queue's lifetime so handles from older batches resolve to detectably
	// stale entities (via generation) instead of dangling.
	std::unordered_map<uint64_t, Entity> _deferredEntities;
	uint64_t _nextDeferredEntityId = 1;
	std::mutex _mutex;

	std::vector<DeferredCommand>& deferredQueue(std::string_view smName)
	{
		return _deferredQueues[std::string(smName)];
	}

	Entity resolveDeferredToken(uint64_t tokenId, std::string_view opName) const
	{
		auto it = _deferredEntities.find(tokenId);
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (it != _deferredEntities.end()) return it->second;
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::" << opName
		          << ": deferred entity was never created or its creation was skipped. Command skipped." << std::endl;
#else
		(void)opName;
		return it->second;
#endif
		return Entity::invalid();
	}

	// The trailing SystemManager name controls queue selection and must not be forwarded to the component constructor.
	template <typename... Ts>
	static auto tupleWithoutLast(std::tuple<Ts...>&& value)
	{
		return [&]<std::size_t... I>(std::index_sequence<I...>)
		{
			return std::tuple<std::tuple_element_t<I, std::tuple<Ts...>>...>(std::get<I>(std::move(value))...);
		}(std::make_index_sequence<sizeof...(Ts) - 1>{});
	}

public:
	Entity resolveEntity(DeferredEntity handle) const
	{
		auto it = _deferredEntities.find(handle.id);
		return it == _deferredEntities.end() ? Entity::invalid() : it->second;
	}

	void flushDeferred(GeneralManager& gm, std::string_view smName);

	DeferredEntity createEntity(GeneralManager& gm, std::string_view smName);

	void destroyEntity(GeneralManager& gm, Entity entity, std::string_view smName);
	void destroyEntity(GeneralManager& gm, DeferredEntity handle, std::string_view smName);

	template <typename TComponent, typename... Args>
	void addComponent(GeneralManager& gm, Entity entity, Args&&... args);

	template <typename TComponent, typename... Args>
	void addComponent(GeneralManager& gm, DeferredEntity handle, Args&&... args);

	template <typename TComponent>
	void removeComponent(GeneralManager& gm, Entity entity, std::string_view smName);

	template <typename TComponent>
	void removeComponent(GeneralManager& gm, DeferredEntity handle, std::string_view smName);

	template <typename TSystem>
	void subscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName);

	template <typename TSystem>
	void subscribeEntity(GeneralManager& gm, DeferredEntity handle, std::string_view smName);

	template <typename TSystem>
	void unsubscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName);

	template <typename TSystem>
	void unsubscribeEntity(GeneralManager& gm, DeferredEntity handle, std::string_view smName);
};

} // namespace Orhescyon
