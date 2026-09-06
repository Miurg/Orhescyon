#pragma once

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

class DeferredChangeQueue
{
private:
	enum class DeferredOpKind : uint8_t
	{
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

		DeferredCommand(DeferredOpKind kind, std::function<void(GeneralManager&)> apply)
		    : kind(kind), apply(std::move(apply))
		{}

		DeferredCommand(const DeferredCommand&) = delete;
		DeferredCommand& operator=(const DeferredCommand&) = delete;
		DeferredCommand(DeferredCommand&&) noexcept = default;
		DeferredCommand& operator=(DeferredCommand&&) noexcept = default;

		void execute(GeneralManager& gm)
		{
			apply(gm);
		}
	};

	std::unordered_map<std::string, std::vector<DeferredCommand>> _deferredQueues;
	std::mutex _mutex;

	std::vector<DeferredCommand>& deferredQueue(std::string_view smName)
	{
		return _deferredQueues[std::string(smName)];
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

	void flushDeferred(GeneralManager& gm, std::string_view smName);

	void destroyEntity(GeneralManager& gm, Entity entity, std::string_view smName);

	template <typename TComponent, typename... Args>
	void addComponent(GeneralManager& gm, Entity entity, Args&&... args);

	template <typename TComponent>
	void removeComponent(GeneralManager& gm, Entity entity, std::string_view smName);

	template <typename TSystem>
	void subscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName);

	template <typename TSystem>
	void unsubscribeEntity(GeneralManager& gm, Entity entity, std::string_view smName);
};

} // namespace Orhescyon
