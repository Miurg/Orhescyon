#pragma once

#include <atomic>
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
class DeferredChangeQueue;

// The state stays alive while either an external handle or a queued command can still reach it.
struct DeferredEntity
{
	std::atomic<Entity> entity{Entity::invalid()};
	std::atomic_uint32_t referenceCount{0};

	void retain() noexcept
	{
		referenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	void release() noexcept
	{
		if (referenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
	}

private:
	friend class DeferredChangeQueue;

	DeferredEntity() noexcept = default;
	~DeferredEntity() = default;
};

struct DeferredEntityHandle
{
private:
	friend class DeferredChangeQueue;

	DeferredEntity* _entity = nullptr;

	explicit DeferredEntityHandle(DeferredEntity* entity) noexcept : _entity(entity)
	{
		if (_entity) _entity->retain();
	}

public:
	DeferredEntityHandle() noexcept = default;

	DeferredEntityHandle(const DeferredEntityHandle& other) noexcept : DeferredEntityHandle(other._entity) {}

	DeferredEntityHandle(DeferredEntityHandle&& other) noexcept : _entity(std::exchange(other._entity, nullptr)) {}

	DeferredEntityHandle& operator=(const DeferredEntityHandle& other) noexcept
	{
		if (this == &other) return *this;
		DeferredEntityHandle copy(other);
		std::swap(_entity, copy._entity);
		return *this;
	}

	DeferredEntityHandle& operator=(DeferredEntityHandle&& other) noexcept
	{
		if (this == &other) return *this;
		if (_entity) _entity->release();
		_entity = std::exchange(other._entity, nullptr);
		return *this;
	}

	~DeferredEntityHandle()
	{
		if (_entity) _entity->release();
	}

	[[nodiscard]] bool operator==(const DeferredEntityHandle& other) const noexcept
	{
		return _entity == other._entity;
	}
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
		DeferredEntity* entity;
		std::function<void(GeneralManager&)> apply;

		DeferredCommand(DeferredOpKind kind, std::function<void(GeneralManager&)> apply)
		    : kind(kind), entity(nullptr), apply(std::move(apply))
		{}

		DeferredCommand(DeferredOpKind kind, DeferredEntity* entity, std::function<void(GeneralManager&)> apply)
		    : kind(kind), entity(entity), apply(std::move(apply))
		{
			if (entity) entity->retain();
		}

		DeferredCommand(const DeferredCommand&) = delete;
		DeferredCommand& operator=(const DeferredCommand&) = delete;

		DeferredCommand(DeferredCommand&& other) noexcept
		    : kind(other.kind), entity(std::exchange(other.entity, nullptr)), apply(std::move(other.apply))
		{}

		DeferredCommand& operator=(DeferredCommand&& other) noexcept
		{
			if (this == &other) return *this;
			if (entity) entity->release();
			kind = other.kind;
			entity = std::exchange(other.entity, nullptr);
			apply = std::move(other.apply);
			return *this;
		}

		~DeferredCommand()
		{
			if (entity) entity->release();
		}

		void execute(GeneralManager& gm)
		{
			DeferredEntity* executedEntity = std::exchange(entity, nullptr);
			try
			{
				apply(gm);
			}
			catch (...)
			{
				if (executedEntity) executedEntity->release();
				throw;
			}
			if (executedEntity) executedEntity->release();
		}
	};

	std::unordered_map<std::string, std::vector<DeferredCommand>> _deferredQueues;
	std::mutex _mutex;

	std::vector<DeferredCommand>& deferredQueue(std::string_view smName)
	{
		return _deferredQueues[std::string(smName)];
	}

	Entity resolveDeferredEntity(const DeferredEntity* deferredEntity, std::string_view opName) const
	{
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (deferredEntity) return deferredEntity->entity;
		std::cerr << "WARNING::DEFERRED_CHANGE_QUEUE::" << opName
		          << ": deferred entity was never created or its creation was skipped. Command skipped." << std::endl;
#else
		(void)opName;
		return deferredEntity->entity;
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
	~DeferredChangeQueue()
	{
		std::scoped_lock lock(_mutex);
		_deferredQueues.clear();
	}

	Entity resolveEntity(const DeferredEntityHandle& handle) const
	{
		return handle._entity ? static_cast<Entity>(handle._entity->entity) : Entity::invalid();
	}

	void flushDeferred(GeneralManager& gm, std::string_view smName);

	DeferredEntityHandle createEntity(GeneralManager& gm, std::string_view smName);

	void destroyEntity(GeneralManager& gm, Entity entity, std::string_view smName);
	void destroyEntity(GeneralManager& gm, const DeferredEntityHandle& handle, std::string_view smName);

	template <typename TComponent, typename... Args>
	void addComponent(GeneralManager& gm, Entity entity, Args&&... args);

	template <typename TComponent, typename... Args>
	void addComponent(GeneralManager& gm, const DeferredEntityHandle& handle, Args&&... args);

	template <typename TComponent>
	void removeComponent(GeneralManager& gm, Entity entity, std::string_view smName);

	template <typename TComponent>
	void removeComponent(GeneralManager& gm, const DeferredEntityHandle& handle, std::string_view smName);

	template <typename TSystem>
	void subscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName);

	template <typename TSystem>
	void subscribeEntity(GeneralManager& gm, const DeferredEntityHandle& handle, std::string_view smName);

	template <typename TSystem>
	void unsubscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName);

	template <typename TSystem>
	void unsubscribeEntity(GeneralManager& gm, const DeferredEntityHandle& handle, std::string_view smName);
};

} // namespace Orhescyon
