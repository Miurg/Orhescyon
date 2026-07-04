#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

namespace Orhescyon
{
// Dynamic bitset over entity slots. Backs component presence masks and
// per-system subscription sets; joins are bitwise ANDs of whole words.
// set() grows storage on demand; test()/clear() treat slots beyond capacity as unset.
class SlotBitmap
{
	static constexpr uint32_t WORD_SHIFT = 6;
	static constexpr uint32_t WORD_MASK = 63;

	std::vector<uint64_t> _words;
	uint32_t _setBitCount = 0;

public:
	void set(uint32_t slot)
	{
		// find our word
		const uint32_t wordIndex = slot >> WORD_SHIFT;
		if (wordIndex >= _words.size())
		{
			// amortized doubling keeps sequential growth linear overall
			_words.resize(std::max<size_t>(wordIndex + 1, _words.size() * 2), 0);
		}

		// create our bit mask
		const uint64_t bit = uint64_t{1} << (slot & WORD_MASK);

		uint64_t& word = _words[wordIndex];

		// set bit in word
		if ((word & bit) == 0)
		{
			word |= bit;
			++_setBitCount;
		}
	}

	void clear(uint32_t slot) noexcept
	{
		// find our word
		const uint32_t wordIndex = slot >> WORD_SHIFT;
		if (wordIndex >= _words.size()) return;

		// create our bit mask
		const uint64_t bit = uint64_t{1} << (slot & WORD_MASK);

		uint64_t& word = _words[wordIndex];

		// clear bit in word
		if ((word & bit) != 0)
		{
			word &= ~bit;
			--_setBitCount;
		}
	}

	[[nodiscard]] bool test(uint32_t slot) const noexcept
	{
		// find our word
		const uint32_t wordIndex = slot >> WORD_SHIFT;
		if (wordIndex >= _words.size()) return false;

		// find state of bit
		return (_words[wordIndex] >> (slot & WORD_MASK) & 1) != 0;
	}

	// Out-of-range words read as zero so joins can iterate to the widest bitmap.
	[[nodiscard]] uint64_t word(uint32_t wordIndex) const noexcept
	{
		if (wordIndex >= _words.size()) return 0;
		return _words[wordIndex];
	}

	[[nodiscard]] uint32_t wordCount() const noexcept
	{
		return static_cast<uint32_t>(_words.size());
	}

	[[nodiscard]] uint32_t setBitCount() const noexcept
	{
		return _setBitCount;
	}

	// Grows word storage to cover slotCount slots; never shrinks.
	void reserveSlots(uint32_t slotCount)
	{
		const size_t wordsNeeded = (static_cast<size_t>(slotCount) + WORD_MASK) >> WORD_SHIFT;
		if (wordsNeeded > _words.size())
		{
			_words.resize(wordsNeeded, 0);
		}
	}

	// Clears all bits but keeps capacity for reuse.
	void clearAll() noexcept
	{
		std::fill(_words.begin(), _words.end(), uint64_t{0});
		_setBitCount = 0;
	}

	// Calls func(slot) for every set bit in ascending slot order.
	template <typename TFunc>
	void forEachSetBit(TFunc&& func) const
	{
		const uint32_t words = wordCount();
		for (uint32_t wordIndex = 0; wordIndex < words; ++wordIndex)
		{
			uint64_t word = _words[wordIndex];
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
