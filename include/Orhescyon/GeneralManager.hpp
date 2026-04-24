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
#include "Entitys/ActiveEntitySet.hpp"
#include "Systems/SystemManager.hpp"
#include "Contexts/ContextManager.hpp"

namespace Orhescyon
{
// Central ECS manager — coordinates entities, components, systems and contexts.
class GeneralManager
{
private:
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
	GeneralManager(GeneralManager&&) noexcept = default;
	GeneralManager& operator=(GeneralManager&&) noexcept = default;

	GeneralManager()
	{
		registerSystemManager("default");
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
	Entity createEntity()
	{
		Entity entity = _entityManager.createEntity();
		return entity;
	}

	// Removes components first, then unsubscribes from all systems, then deactivates.
	void destroyEntity(Entity entity)
	{
		_entityManager.destroyEntity(entity);
		_componentManager.removeEntity(entity);
		for (auto& [name, sm] : _systemManagers)
		{
			sm.unsubscribeFromAll(entity, *this);
		}
	}

	// Adds a component to the entity. Returns pointer to it or nullptr if inactive.
	template <typename TComponent, typename... Args>
	TComponent* addComponent(Entity entity, Args&&... args)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::AddComponent on inactive entity " << entity << std::endl;
			return nullptr;
		}
#endif

		TComponent* component = _componentManager.addComponent<TComponent>(entity, std::forward<Args>(args)...);

		return component;
	}

	// Removes a component and auto-unsubscribes from systems whose requirements no longer match.
	template <typename TComponent>
	void removeComponent(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::RemoveComponent on inactive entity " << entity << std::endl;
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

	// Creates an empty SystemManager with the given name. Duplicate names warn+skip under HIGH_CHECK.
	void registerSystemManager(std::string name)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (_systemManagers.find(name) != _systemManagers.end())
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

	// Registers a new system;
	template <typename TSystem, typename... Args>
	void registerSystem(Args&&... args)
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
			return;
		}
#endif
		_systemTypeToManager.insert_or_assign(std::type_index(typeid(TSystem)), smName);
		sm->addSystem<TSystem>(*this, std::move(system));
	}

	// Subscribes an entity to a system.
	template <typename TSystem>
	void subscribeEntity(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::SubscribeEntity on inactive entity " << entity << std::endl;
			return;
		}
#endif

		auto it = _systemTypeToManager.find(std::type_index(typeid(TSystem)));
#ifdef ORHESCYON_HIGH_CHECK
		if (it == _systemTypeToManager.end())
		{
			std::cerr << "WARNING::GENERAL_MANAGER::SubscribeEntity: system " << typeid(TSystem).name()
			          << " not registered in any SystemManager" << std::endl;
			return;
		}
#endif
		SystemManager* sm = findSystemManager(it->second);
		sm->subscribe<TSystem>(entity, *this);
	}

	// Unsubscribes an entity from a system.
	template <typename TSystem>
	void unsubscribeEntity(Entity entity)
	{
#ifdef ORHESCYON_HIGH_CHECK
		if (!_entityManager.isActive(entity))
		{
			std::cerr << "WARNING::GENERAL_MANAGER::UnsubscribeEntity on inactive entity " << entity << std::endl;
			return;
		}
#endif

		auto it = _systemTypeToManager.find(std::type_index(typeid(TSystem)));
#ifdef ORHESCYON_HIGH_CHECK
		if (it == _systemTypeToManager.end())
		{
			std::cerr << "WARNING::GENERAL_MANAGER::UnsubscribeEntity: system " << typeid(TSystem).name()
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
		sm->updateSystems(*this);
	}

	// Update default SystemManager.
	void update()
	{
		update("default");
	}

	const ActiveEntitySet& getActiveEntities() const noexcept
	{
		return _entityManager.getActiveEntities();
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
