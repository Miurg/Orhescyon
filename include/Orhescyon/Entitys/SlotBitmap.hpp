#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>

namespace Orhescyon
{
// Dynamic bitset over entity slots. Backs component presence masks and
// per-system subscription sets; joins are bitwise ANDs of whole words.
// set() grows storage on demand; test()/clear() treat slots beyond capacity as unset.
class SlotBitmap
{
	static constexpr uint32_t WORD_SHIFT = 6;
	static constexpr uint32_t WORD_MASK = 63;
	static constexpr uint32_t MAX_WORD_COUNT = uint32_t{1} << (32 - WORD_SHIFT);

	using AtomicWord = std::atomic<uint64_t>;

	static_assert(AtomicWord::is_always_lock_free && std::atomic<AtomicWord*>::is_always_lock_free
		&& std::atomic<uint32_t>::is_always_lock_free && std::atomic<int64_t>::is_always_lock_free,
		"SlotBitmap requires lock-free pointer and integer atomics");

	// Word pages cover [0], [1], [2..3], ... without moving entries during growth.
	std::array<std::atomic<AtomicWord*>, 33 - WORD_SHIFT> _words{};
	std::atomic<uint32_t> _wordCount{0};
	std::atomic<int64_t> _setBitCount{0};

	static uint32_t pageStart(uint32_t pageIndex) noexcept
	{
		return pageIndex == 0 ? 0 : uint32_t{1} << (pageIndex - 1);
	}

	void ensureWordCount(uint32_t count)
	{
		uint32_t current = _wordCount.load(std::memory_order_acquire);
		if (count <= current) return;

		const uint32_t lastPage = std::bit_width(count - 1);
		const uint32_t firstPage = current == 0 ? 0 : std::bit_width(current - 1);
		for (uint32_t pageIndex = firstPage; pageIndex <= lastPage; ++pageIndex)
		{
			AtomicWord* page = _words[pageIndex].load(std::memory_order_acquire);
			if (!page)
			{
				auto candidate = std::make_unique<AtomicWord[]>(pageIndex == 0 ? 1 : pageStart(pageIndex));
				if (_words[pageIndex].compare_exchange_strong(page, candidate.get(),
					std::memory_order_acq_rel, std::memory_order_acquire))
				{
					page = candidate.release();
				}
			}
		}
		while (current < count && !_wordCount.compare_exchange_weak(current, count,
			std::memory_order_release, std::memory_order_relaxed))
		{
		}
	}

	AtomicWord& wordEntry(uint32_t wordIndex) const noexcept
	{
		const uint32_t pageIndex = std::bit_width(wordIndex);
		AtomicWord* page = _words[pageIndex].load(std::memory_order_acquire);
		return page[wordIndex - pageStart(pageIndex)];
	}

public:
	~SlotBitmap()
	{
		for (auto& page : _words)
		{
			delete[] page.load(std::memory_order_relaxed);
		}
	}

	void set(uint32_t slot)
	{
		// find our word
		const uint32_t wordIndex = slot >> WORD_SHIFT;
		const uint32_t words = wordCount();
		if (wordIndex >= words)
		{
			// amortized doubling keeps sequential growth linear overall
			ensureWordCount(std::min(MAX_WORD_COUNT, std::max(wordIndex + 1, words * 2)));
		}

		// create our bit mask
		const uint64_t bit = uint64_t{1} << (slot & WORD_MASK);

		AtomicWord& word = wordEntry(wordIndex);

		// set bit in word
		if ((word.fetch_or(bit, std::memory_order_acq_rel) & bit) == 0)
		{
			_setBitCount.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void clear(uint32_t slot) noexcept
	{
		// find our word
		const uint32_t wordIndex = slot >> WORD_SHIFT;
		if (wordIndex >= wordCount()) return;

		// create our bit mask
		const uint64_t bit = uint64_t{1} << (slot & WORD_MASK);

		AtomicWord& word = wordEntry(wordIndex);

		// clear bit in word
		if ((word.fetch_and(~bit, std::memory_order_acq_rel) & bit) != 0)
		{
			_setBitCount.fetch_sub(1, std::memory_order_relaxed);
		}
	}

	[[nodiscard]] bool test(uint32_t slot) const noexcept
	{
		// find our word
		const uint32_t wordIndex = slot >> WORD_SHIFT;
		if (wordIndex >= wordCount()) return false;

		// find state of bit
		return (wordEntry(wordIndex).load(std::memory_order_acquire) >> (slot & WORD_MASK) & 1) != 0;
	}

	// Out-of-range words read as zero so joins can iterate to the widest bitmap.
	[[nodiscard]] uint64_t word(uint32_t wordIndex) const noexcept
	{
		if (wordIndex >= wordCount()) return 0;
		return wordEntry(wordIndex).load(std::memory_order_acquire);
	}

	[[nodiscard]] uint32_t wordCount() const noexcept
	{
		return _wordCount.load(std::memory_order_acquire);
	}

	[[nodiscard]] uint32_t setBitCount() const noexcept
	{
		const int64_t count = _setBitCount.load(std::memory_order_relaxed);
		return static_cast<uint32_t>(std::clamp<int64_t>(count, 0, std::numeric_limits<uint32_t>::max()));
	}

	// Grows word storage to cover slotCount slots; never shrinks.
	void reserveSlots(uint32_t slotCount)
	{
		const uint32_t wordsNeeded = (slotCount >> WORD_SHIFT) + ((slotCount & WORD_MASK) != 0);
		ensureWordCount(wordsNeeded);
	}

	void clearAll() noexcept
	{
		const uint32_t words = wordCount();
		for (uint32_t wordIndex = 0; wordIndex < words; ++wordIndex)
		{
			const uint64_t previous = wordEntry(wordIndex).exchange(0, std::memory_order_acq_rel);
			_setBitCount.fetch_sub(std::popcount(previous), std::memory_order_relaxed);
		}
	}

	// Calls func(slot) for every set bit in ascending slot order.
	template <typename TFunc>
	void forEachSetBit(TFunc&& func) const
	{
		const uint32_t words = wordCount();
		for (uint32_t wordIndex = 0; wordIndex < words; ++wordIndex)
		{
			uint64_t word = wordEntry(wordIndex).load(std::memory_order_acquire);
			const uint32_t baseSlot = wordIndex << WORD_SHIFT;
			while (word != 0)
			{
				// find our bit
				const uint32_t bitIndex = static_cast<uint32_t>(std::countr_zero(word));

				func(baseSlot + bitIndex);

				// thanks, Brian Kernighan
				word &= word - 1;
			}
		}
	}
};

} // namespace Orhescyon
