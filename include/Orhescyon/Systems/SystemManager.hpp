#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <queue>
#include <set>

#include "ISystemCore.hpp"
#include "../Entitys/ActiveEntitySet.hpp"
#include "../Jobs/IJobSystem.hpp"

namespace Orhescyon
{
class GeneralManager;
// Owns all systems and tracks bidirectional entity↔system subscriptions.
// subscribe() recursively resolves system dependencies.
class SystemManager
{
private:
	std::vector<std::unique_ptr<ISystemCore>> SystemCore;
	std::unordered_map<std::type_index, ActiveEntitySet> SystemToEntities;
	std::unordered_map<Entity, std::vector<std::type_index>> EntityToSystems;

	void subscribeInternal(Entity entity, std::type_index systemType, GeneralManager& gm)
	{
		ISystemCore* system = nullptr;

		for (auto& sys : SystemCore)
		{
			if (std::type_index(typeid(*sys)) == systemType)
			{
				system = sys.get();
				break;
			}
		}
#ifdef ORHESCYON_HIGH_CHECK
		if (!system)
		{
			std::cerr << "WARNING::SYSTEM_MANAGER::Entity " << entity << " trying to subscribe to a non-existent system "
			          << systemType.name() << ". Cant subscribe!" << std::endl;
			return;
		}
#endif

#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (!system->shouldProcessEntity(entity, gm))
		{
			std::cerr << "WARNING::SYSTEM_MANAGER::Entity " << entity
			          << " doesn't have all required components for system " << systemType.name() << ". Cant subscribe!"
			          << std::endl;
			return;
		}
#endif

		auto& currentSystems = EntityToSystems[entity];

#ifdef ORHESCYON_HIGH_CHECK
		if (std::find(currentSystems.begin(), currentSystems.end(), systemType) != currentSystems.end())
		{
			std::cout << "WARNING::SYSTEM_MANAGER::Entity " << entity
			          << " trying to subscribe to a system that is already subscribed to  " << systemType.name()
			          << ". Cant subscribe!" << std::endl;
			return;
		}
#endif

		system->onEntitySubscribed(entity, gm);

		SystemToEntities[systemType].insert(entity);
		EntityToSystems[entity].push_back(systemType);

		const auto& dependencies = system->getSystemDependencies();
		for (const auto& depType : dependencies)
		{
			subscribeInternal(entity, depType, gm);
		}
	}

	bool _executionOrderDirty = true;
	std::vector<std::vector<ISystemCore*>> _executionLayers;

public:
	void onShutdown(GeneralManager& gm)
	{
		for (auto it = SystemCore.rbegin(); it != SystemCore.rend(); ++it)
		{
			(*it)->onShutdown(gm);
		}
	}

	template <typename TSystem>
	void addSystem(GeneralManager& gm, std::unique_ptr<TSystem> system)
	{
		static_assert(std::is_base_of_v<ISystemCore, TSystem>, "TSystem must derive from ISystemCore");
		SystemCore.push_back(std::move(system));
		SystemCore.back()->onRegistered(gm);
		_executionOrderDirty = true;
	}

	template <typename TSystem>
	void subscribe(Entity entity, GeneralManager& gm)
	{
		subscribeInternal(entity, typeid(TSystem), gm);
	}

	void unsubscribe(Entity entity, std::type_index systemType, GeneralManager& gm)
	{
		ISystemCore* system = nullptr;

		{
			for (auto& sys : SystemCore)
			{
				if (std::type_index(typeid(*sys)) == systemType)
				{
					system = sys.get();
					break;
				}
			}
#ifdef ORHESCYON_HIGH_CHECK
			if (!system)
			{
				std::cerr << "WARNING::SYSTEM_MANAGER::Entity " << entity
				          << " trying to unsubscribe from a non-existent system " << systemType.name()
				          << ". Cant unsubscribe!" << std::endl;
				return;
			}
#endif
		}

		system->onEntityUnsubscribed(entity, gm);

		auto systemIt = SystemToEntities.find(systemType);
		if (systemIt != SystemToEntities.end())
		{
			systemIt->second.erase(entity);
		}

		auto entityIt = EntityToSystems.find(entity);
		if (entityIt != EntityToSystems.end())
		{
			auto& systems = entityIt->second;
			auto systemIt = std::find(systems.begin(), systems.end(), systemType);
			if (systemIt != systems.end())
			{
				systems.erase(systemIt);
			}
		}
	}

	template <typename TSystem>
	void unsubscribe(Entity entity, GeneralManager& gm)
	{
		unsubscribe(entity, std::type_index(typeid(TSystem)), gm);
	}

	void unsubscribeFromAll(Entity entity, GeneralManager& gm)
	{
		auto it = EntityToSystems.find(entity);
		if (it != EntityToSystems.end())
		{
			std::vector<std::type_index> systems = it->second;
			for (const auto& systemType : systems)
			{
				unsubscribe(entity, systemType, gm);
			}
		}
	}

	void checkEntitySubscriptions(Entity entity, GeneralManager& gm)
	{
		auto entityIt = EntityToSystems.find(entity);
		if (entityIt == EntityToSystems.end()) return;

		auto& entitySystems = entityIt->second;

		for (auto it = entitySystems.begin(); it != entitySystems.end();)
		{
			std::type_index systemType = *it;

			bool shouldRemove = true;
			for (auto& system : SystemCore)
			{
				if (std::type_index(typeid(*system)) == systemType)
				{
					shouldRemove = !system->shouldProcessEntity(entity, gm);
					break;
				}
			}

			if (shouldRemove)
			{
				SystemToEntities[systemType].erase(entity);
				it = entitySystems.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void resolveExecutionSequence()
	{
		if (!_executionOrderDirty) return;
		_executionLayers.clear();
		if (SystemCore.empty()) return;

		size_t systemsSize = SystemCore.size();
		std::vector<std::vector<size_t>> neighbours(systemsSize);
		std::vector<int> inDegree(systemsSize, 0);
		std::set<std::pair<size_t, size_t>> addedEdges;

		auto addEdge = [&](size_t from, size_t to)
		{
			if (addedEdges.insert({from, to}).second)
			{
				neighbours[from].push_back(to);
				inDegree[to]++;
			}
		};

		std::unordered_map<std::type_index, size_t> typeToIndex;
		for (size_t i = 0; i < systemsSize; ++i) typeToIndex[std::type_index(typeid(*SystemCore[i]))] = i;

		for (size_t first = 0; first < systemsSize; ++first)
		{
			const auto writesFirst = SystemCore[first]->getWriteComponents();
			const auto readsFirst = SystemCore[first]->getReadComponents();

			for (size_t second = first + 1; second < systemsSize; ++second)
			{
				const auto writesSecond = SystemCore[second]->getWriteComponents();
				const auto readsSecond = SystemCore[second]->getReadComponents();

				// first writes what second reads or writes - first must come before second
				bool firstBeforeSecond = false;
				for (auto& writeFirstInst : writesFirst)
				{
					for (auto& readsSecondInst : readsSecond)
						if (writeFirstInst == readsSecondInst)
						{
							firstBeforeSecond = true;
							break;
						}
					for (auto& writeSecondInst : writesSecond)
						if (writeFirstInst == writeSecondInst)
						{
							firstBeforeSecond = true;
							break;
						}
					if (firstBeforeSecond) break;
				}

				// second writes what first reads - second must come before first
				bool secondBeforeFirst = false;
				for (auto& writesSecondInst : writesSecond)
				{
					for (auto& readsFirstInst : readsFirst)
						if (writesSecondInst == readsFirstInst)
						{
							secondBeforeFirst = true;
							break;
						}
					if (secondBeforeFirst) break;
				}

#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
				if (firstBeforeSecond && secondBeforeFirst)
				{
					std::cerr << "WARNING::SYSTEM_MANAGER::Circular data dependency between "
					          << typeid(*SystemCore[first]).name() << " and " << typeid(*SystemCore[second]).name()
					          << std::endl;
				}
#endif
				if (firstBeforeSecond) addEdge(first, second);
				if (secondBeforeFirst) addEdge(second, first);
			}

			// Explicit dependencies
			for (auto& before : SystemCore[first]->getBeforeSystems())
			{
				auto it = typeToIndex.find(before);
				if (it != typeToIndex.end()) addEdge(first, it->second);
			}
			for (auto& after : SystemCore[first]->getAfterSystems())
			{
				auto it = typeToIndex.find(after);
				if (it != typeToIndex.end()) addEdge(it->second, first);
			}
		}

		std::queue<size_t> q;
		for (size_t i = 0; i < systemsSize; ++i)
			if (inDegree[i] == 0) q.push(i);

		size_t processed = 0;
		while (!q.empty())
		{
			std::vector<ISystemCore*> layer;
			size_t layerSize = q.size();
			for (size_t i = 0; i < layerSize; ++i)
			{
				size_t curr = q.front();
				q.pop();
				layer.push_back(SystemCore[curr].get());
				for (size_t next : neighbours[curr])
					if (--inDegree[next] == 0) q.push(next);
			}
			_executionLayers.push_back(std::move(layer));
			processed += layerSize;
		}

#if defined(ORHESCYON_LOW_CHECK) || defined(ORHESCYON_HIGH_CHECK)
		if (processed != systemsSize)
		{
			throw std::runtime_error("SYSTEM_MANAGER: Cycle detected in system dependency graph! " +
			                         std::to_string(systemsSize - processed) + " system(s) involved.");
		}
#endif

		_executionOrderDirty = false;
	}

	void updateSystems(GeneralManager& gm, IJobSystem& jobSystem)
	{
		if (_executionOrderDirty) resolveExecutionSequence();

		for (auto& layer : _executionLayers)
		{
			if (layer.size() == 1)
			{
				layer[0]->update(gm);
			}
			else
			{
				jobSystem.parallelFor(layer.size(), [&layer, &gm](std::size_t index) { layer[index]->update(gm); });
			}
		}
	}
};

} // namespace Orhescyon
