#pragma once

#include <typeindex>
#include <vector>

namespace Orhescyon
{
// Scheduling declarations of one system type, filled through SystemRegistration.
struct SystemSchedulingMetadata
{
	std::vector<std::type_index> readComponents;
	std::vector<std::type_index> writeComponents;
	std::vector<std::type_index> beforeSystems;
	std::vector<std::type_index> afterSystems;
};
}
