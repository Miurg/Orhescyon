#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "../Components/ComponentManager.hpp"
#include "../Entitys/Entity.hpp"
#include "../Entitys/EntityManager.hpp"
#include "../Entitys/SlotBitmap.hpp"
#include "../Jobs/IJobSystem.hpp"

namespace Orhescyon
{
namespace detail
{
template <typename TStorages>
uint32_t subscribedEntityJoinWordCount(const SlotBitmap& subscriptionBits, const TStorages& storages)
{
	// just find minimum count storage
	uint32_t wordCount = subscriptionBits.wordCount();
	std::apply([&](auto*... storage) { ((wordCount = std::min(wordCount, storage->presenceWordCount())), ...); },
	           storages);
	return wordCount;
}

template <typename... TComponents, typename TStorages, typename TFunc>
void forEachSubscribedEntityJoinWords(const SlotBitmap& subscriptionBits, const EntityManager& entityManager,
                                    const TStorages& storages, uint32_t beginWord, uint32_t endWord, TFunc& func)
{
	// find if all components are column based and we dont have sparse
	constexpr bool allContiguous =
	    (ComponentManager::StorageFor<std::remove_const_t<TComponents>>::CONTIGUOUS_DATA && ...);

	// each call works only on its part of bitmap
	for (uint32_t wordIndex = beginWord; wordIndex < endWord; ++wordIndex)
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
				std::apply(
				    [&](auto*... storage)
				    {
					    // get pointers to first components in this word
					    auto runs = std::make_tuple(storage->componentRunPointer(wordIndex)...);
					    // all bits are set, so no need to check them one by one
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

}

template <typename... TComponents, typename TFunc>
void forEachSubscribedEntityJoin(const SlotBitmap& subscriptionBits, const EntityManager& entityManager,
                                 ComponentManager& componentManager, TFunc&& func)
{
	// poitners to components storages
	auto storages = std::make_tuple(&componentManager.getStorage<std::remove_const_t<TComponents>>()...);

	// how many words we need to check
	const uint32_t wordCount = detail::subscribedEntityJoinWordCount(subscriptionBits, storages);

	// go through all words, func can create entities but other structural changes must wait
	detail::forEachSubscribedEntityJoinWords<TComponents...>(subscriptionBits, entityManager, storages, 0, wordCount,
	                                                       func);
}

template <typename... TComponents, typename TFunc>
void parallelForEachSubscribedEntityJoin(const SlotBitmap& subscriptionBits, const EntityManager& entityManager,
                                        ComponentManager& componentManager, IJobSystem& jobSystem, TFunc&& func)
{
	// no subscribers == nothing to do
	if (subscriptionBits.setBitCount() == 0) return;

	// get storages once, jobs will use same pointers
	auto storages = std::make_tuple(&componentManager.getStorage<std::remove_const_t<TComponents>>()...);

	const uint32_t wordCount = detail::subscribedEntityJoinWordCount(subscriptionBits, storages);

	// if some storage is empty then no entity can match
	if (wordCount == 0) return;

	// split by words, so each iteration gets its own 64 slots
	// jobs use local storages by reference, so parallelFor must wait for all of them
	jobSystem.parallelFor(wordCount,
	                      [&](std::size_t index)
	                      {
		                      const uint32_t wordIndex = static_cast<uint32_t>(index);
		                      // end is excluded, so +1 gives this iteration only one word
		                      detail::forEachSubscribedEntityJoinWords<TComponents...>(
		                          subscriptionBits, entityManager, storages, wordIndex, wordIndex + 1, func);
	                      });
}

} // namespace Orhescyon
