#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "../Components/ComponentManager.hpp"
#include "../Entitys/Entity.hpp"
#include "../Entitys/EntityManager.hpp"
#include "../Entitys/SlotBitmap.hpp"

namespace Orhescyon
{
// Word-level join of subscription bits with component presence bits.
// Structural changes are forbidden inside func.
template <typename... TComponents, typename TFunc>
void forEachSubscribedEntityJoin(const SlotBitmap& subscriptionBits, const EntityManager& entityManager,
                                 ComponentManager& componentManager, TFunc&& func)
{
	// find if all components are column based and we dont have sparse
	constexpr bool allContiguous =
	    (ComponentManager::StorageFor<std::remove_const_t<TComponents>>::CONTIGUOUS_DATA && ...);

	// gets poitners to components storages
	auto storages = std::make_tuple(&componentManager.getStorage<std::remove_const_t<TComponents>>()...);

	// just find minimum count storage
	uint32_t wordCount = subscriptionBits.wordCount();
	std::apply([&](auto*... storage) { ((wordCount = std::min(wordCount, storage->presenceWordCount())), ...); },
	           storages);

	for (uint32_t wordIndex = 0; wordIndex < wordCount; ++wordIndex)
	{
		// one word = 64 bits, remember it
		uint64_t combined = subscriptionBits.word(wordIndex);

		// get combined subscriber number (so its like we put subscribe = 1101, velocity = 1111, position = 0111, then
		// combined = 0101 - we need slots 0 and 2)
		std::apply([&](auto*... storage) { ((combined &= storage->presenceWord(wordIndex)), ...); }, storages);

		// if in whole index no match then just skip
		if (combined == 0) continue;

		// <<6 - its as multiplication by 2^6, so for example word 3 need to have baseslot 192, so 3*2^6 == 192
		const uint32_t baseSlot = wordIndex << 6;
		
		// if we have sparse component... well, that bad, so avoid it if can
		if constexpr (allContiguous)
		{
			// in case if all bits are 1
			if (combined == ~uint64_t{0})
			{
				// whole word matches — walk contiguous runs
				std::apply(
				    [&](auto*... storage)
				    {
					    auto runs = std::make_tuple(storage->componentRunPointer(wordIndex)...);
					    for (uint32_t offset = 0; offset < 64; ++offset)
					    {
						    const uint32_t slot = baseSlot + offset;
						    std::apply(
						        [&](auto*... run)
						        {
							        func(Entity{slot, entityManager.generationOfSlot(slot)},
							             static_cast<TComponents&>(run[offset])...);
						        },
						        runs);
					    }
				    },
				    storages);
				continue;
			}
		}

		// slow pass for sparse or if not whole word mathes
		while (combined != 0)
		{
			const uint32_t slot = baseSlot + static_cast<uint32_t>(std::countr_zero(combined));
			combined &= combined - 1;
			std::apply(
			    [&](auto*... storage)
			    {
				    func(Entity{slot, entityManager.generationOfSlot(slot)},
				         static_cast<TComponents&>(*storage->componentPointerForSlot(slot))...);
			    },
			    storages);
		}
	}
}

} // namespace Orhescyon
