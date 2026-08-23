#pragma once

// Build-time safety tiers:
//   (none)				 — no runtime checks.
//   ORHESCYON_LOW_CHECK  — cheap guards.
//   ORHESCYON_HIGH_CHECK — LOW + dev diagnostics.

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Components/ComponentManager.hpp"
#include "Entitys/EntityManager.hpp"
#include "Systems/SystemManager.hpp"
#include "Systems/SystemRegistration.hpp"
#include "Contexts/ContextManager.hpp"
#include "Jobs/DefaultJobSystem.hpp"
#include "Views/ComponentView.hpp"

namespace Orhescyon
{
// Central ECS manager — coordinates entities, components, systems and contexts.
class GeneralManager
{
private:
	std::unique_ptr<IJobSystem> _ownedJobSystem;
	IJobSystem* _jobSystem = nullptr;

	ComponentManager _componentManager;
	EntityManager _entityManager;
	ContextManager _contextManager;

	std::unordered_map<std::string, SystemManager> _systemManagers;
	std::vector<std::string> _systemManagerOrder;
	std::unordered_map<std::type_index, std::string> _systemTypeToManager;

	SystemManager* findSystemManager(std::string_view name)
	{
		auto it = _systemManagers.find(std::string(name));
		return it == _systemManagers.end() ? nullptr : &it->second;
	}

public:
	GeneralManager(const GeneralManager&) = delete;
	GeneralManager& operator=(const GeneralManager&) = delete;
	GeneralManager(GeneralManager&&) noexcept = delete;
	GeneralManager& operator=(GeneralManager&&) noexcept = delete;

	GeneralManager()
	{
		_ownedJobSystem = std::make_unique<DefaultJobSystem>();
		_jobSystem = _ownedJobSystem.get();
		registerSystemManager("default");
	}

	explicit GeneralManager(IJobSystem* userJobSystem)
	{
		_jobSystem = userJobSystem;
		registerSystemManager("default");
	}

	// Replaces the active job system. Caller must ensure no update is in flight.
	void setJobSystem(IJobSystem* userJobSystem)
	{
		if (userJobSystem)
		{
			_jobSystem = userJobSystem;
			_ownedJobSystem.reset();
		}
		else
		{
			_ownedJobSystem = std::make_unique<DefaultJobSystem>();
			_jobSystem = _ownedJobSystem.get();
		}
	}

	// Shuts down SystemManagers in reverse registration order before managers are destroyed.
	~GeneralManager()
	{
		for (auto it = _systemManagerOrder.rbegin(); it != _systemManagerOrder.rend(); ++it)
		{
			auto mapIt = _systemManagers.find(*it);
			if (mapIt != _systemManagers.end())
			{
				mapIt->second.onShutdown(*this);
			}
		}
	}

	// Creates a new entity and marks it active.
	Entity createEntityImmediate()
	{
		Entity entity = _entityManager.createEntity();
		return entity;
	}

	// Unsubscribes from systems, removes components, then frees the slot.
	void destroyEntityImmediate(Entity entity)
	{
#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::DestroyEntityImmediate on inactive entity " << entity << std::endl;
			return;
		}
#endif

		for (auto& [name, sm] : _systemManagers)
		{
			sm.unsubscribeFromAll(entity, *this);
		}
		_componentManager.removeEntity(entity);
		_entityManager.destroyEntity(entity);
	}

	// Adds a component to the entity. Returns pointer to it or nullptr if inactive.
	template <typename TComponent, typename... Args>
	TComponent* addComponentImmediate(Entity entity, Args&&... args)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::AddComponentImmediate on inactive entity " << entity << std::endl;
			return nullptr;
		}
#endif

		TComponent* component = _componentManager.addComponent<TComponent>(entity, std::forward<Args>(args)...);

		return component;
	}

	// Returns true if the entity has the component.
	template <typename TComponent>
	bool hasComponent(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::HasComponent on inactive entity " << entity << std::endl;
			return false;
		}
#endif
		return _componentManager.hasComponent<TComponent>(entity);
	}

	// Removes a component and auto-unsubscribes from systems whose requirements no longer match.
	template <typename TComponent>
	void removeComponentImmediate(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::RemoveComponentImmediate on inactive entity " << entity << std::endl;
			return;
		}
#endif

		_componentManager.removeComponent<TComponent>(entity);
		for (auto& [name, sm] : _systemManagers)
		{
			sm.checkEntitySubscriptions(entity, *this);
		}
	}

	// Returns pointer to the component or nullptr.
	template <typename TComponent>
	TComponent* getComponent(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::GetComponent on inactive entity " << entity << std::endl;
			return nullptr;
		}
#endif
		return _componentManager.getComponent<TComponent>(entity);
	}

	// Storage footprint of one component type — the data for choosing its StoragePolicy.
	template <typename TComponent>
	[[nodiscard]] StorageStatistics storageStatistics()
	{
		return _componentManager.getStorage<TComponent>().statistics();
	}

	// Creates an empty SystemManager with the given name. Duplicate names warn+skip under HIGH_CHECK.
	void registerSystemManager(std::string name)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (_systemManagers.contains(name))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::RegisterSystemManager duplicate name \"" << name << "\". Ignored."
			          << std::endl;
			return;
		}
#endif
		_systemManagerOrder.push_back(name);
		_systemManagers.try_emplace(std::move(name));
	}

	// Returns the SystemManager registered under the given name.
	SystemManager& getSystemManager(std::string_view name)
	{
		SystemManager* sm = findSystemManager(name);
#ifdef ORHESCYON_HIGH_CHECK
		if (!sm)
		{
			throw std::runtime_error("GENERAL_MANAGER::getSystemManager: no SystemManager named \"" + std::string(name) +
			                         "\"");
		}
#endif
		return *sm;
	}

	// Registers a new system; chain .before/.after/.reads/.writes on the returned registration.
	template <typename TSystem, typename... Args>
	SystemRegistration<TSystem> registerSystem(Args&&... args)
	{
		static_assert(std::is_base_of_v<ISystemCore, TSystem>, "TSystem must derive from ISystemCore");

		auto system = std::make_unique<TSystem>(std::forward<Args>(args)...);
		std::string smName(system->getSystemManagerName());

		SystemManager* sm = findSystemManager(smName);
#ifdef ORHESCYON_HIGH_CHECK
		if (!sm)
		{
			std::cerr << "WARNING::GENERAL_MANAGER::RegisterSystem: SystemManager \"" << smName
			          << "\" not found. Register it first via registerSystemManager." << std::endl;
			return SystemRegistration<TSystem>(nullptr);
		}
#endif
		_systemTypeToManager.insert_or_assign(std::type_index(typeid(TSystem)), smName);
		sm->addSystem<TSystem>(*this, std::move(system));
		return SystemRegistration<TSystem>(sm);
	}

	// Subscribes an entity to a system.
	template <typename TSystem>
	void subscribeEntityImmediate(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::SubscribeEntityImmediate on inactive entity " << entity << std::endl;
			return;
		}
#endif

		auto it = _systemTypeToManager.find(std::type_index(typeid(TSystem)));
#ifdef ORHESCYON_HIGH_CHECK
		if (it == _systemTypeToManager.end())
		{
			std::cerr << "WARNING::GENERAL_MANAGER::SubscribeEntityImmediate: system " << typeid(TSystem).name()
			          << " not registered in any SystemManager" << std::endl;
			return;
		}
#endif
		SystemManager* sm = findSystemManager(it->second);
		sm->subscribe<TSystem>(entity, *this);
	}

	// Iterates entities subscribed to TSystem that also hold all TComponents.
	// Structural changes are forbidden inside func.
	template <typename TSystem, typename... TComponents, typename TFunc>
	void forEachSubscribedEntityWith(TFunc&& func)
	{
		auto it = _systemTypeToManager.find(std::type_index(typeid(TSystem)));
#ifdef ORHESCYON_HIGH_CHECK
		if (it == _systemTypeToManager.end())
		{
			std::cerr << "WARNING::GENERAL_MANAGER::ForEachSubscribedEntityWith: system " << typeid(TSystem).name()
			          << " not registered in any SystemManager" << std::endl;
			return;
		}
#endif
		SystemManager* sm = findSystemManager(it->second);
		const SlotBitmap* subscriptionBits = sm->subscriptionBitmap(typeid(TSystem));
		if (!subscriptionBits) return;

		forEachSubscribedEntityJoin<TComponents...>(*subscriptionBits, _entityManager, _componentManager,
		                                            std::forward<TFunc>(func));
	}

	// Returns true if the entity is subscribed to the given system.
	template <typename TSystem>
	bool isSubscribedTo(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::IsSubscribedTo on inactive entity " << entity << std::endl;
			return false;
		}
#endif
		auto it = _systemTypeToManager.find(std::type_index(typeid(TSystem)));
#ifdef ORHESCYON_HIGH_CHECK
		if (it == _systemTypeToManager.end())
		{
			std::cerr << "WARNING::GENERAL_MANAGER::IsSubscribedTo: system " << typeid(TSystem).name()
			          << " not registered in any SystemManager" << std::endl;
			return false;
		}
#endif
		SystemManager* sm = findSystemManager(it->second);
		return sm->isSubscribed<TSystem>(entity);
	}

	// Unsubscribes an entity from a system.
	template <typename TSystem>
	void unsubscribeEntityImmediate(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::UnsubscribeEntityImmediate on inactive entity " << entity << std::endl;
			return;
		}
#endif

		auto it = _systemTypeToManager.find(std::type_index(typeid(TSystem)));
#ifdef ORHESCYON_HIGH_CHECK
		if (it == _systemTypeToManager.end())
		{
			std::cerr << "WARNING::GENERAL_MANAGER::UnsubscribeEntityImmediate: system " << typeid(TSystem).name()
			          << " not registered in any SystemManager" << std::endl;
			return;
		}
#endif
		SystemManager* sm = findSystemManager(it->second);
		sm->unsubscribe<TSystem>(entity, *this);
	}

	// Updates SystemManager with the given name.
	void update(std::string_view name)
	{
		SystemManager* sm = findSystemManager(name);
#ifdef ORHESCYON_HIGH_CHECK
		if (!sm)
		{
			throw std::runtime_error("GENERAL_MANAGER::update: no SystemManager named \"" + std::string(name) + "\"");
		}
#endif
		sm->updateSystems(*this, *_jobSystem);
	}

	// Update default SystemManager.
	void update()
	{
		update("default");
	}

	// Calls func(Entity) for every live entity in ascending slot order.
	template <typename TFunc>
	void forEachActiveEntity(TFunc&& func) const
	{
		_entityManager.forEachActiveEntity(std::forward<TFunc>(func));
	}

	[[nodiscard]] uint32_t activeEntityCount() const noexcept
	{
		return _entityManager.activeEntityCount();
	}

	bool isActive(Entity entity) const noexcept
	{
		return _entityManager.isActive(entity);
	}

	// Registers a context — a named entity for global resource access.
	template <typename TContext>
	void registerContext(Entity ctx)
	{
		_contextManager.registerContext<TContext>(ctx);
	}

	template <typename TContext>
	Entity getContext()
	{
		return _contextManager.getContext<TContext>();
	}

	// Gets a component from a context entity. Skips the active-entity check.
	template <typename TContext, typename TComponent>
	TComponent* getContextComponent()
	{
		return _componentManager.getComponent<TComponent>(_contextManager.getContext<TContext>());
	}
};
} // namespace Orhescyon
