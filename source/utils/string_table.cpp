#include <utils/string_table.h>

namespace chord
{
	StringTable& StringTable::get()
	{
		static StringTable table { };
		return table;
	}

	uint64 StringTable::update(std::string_view str)
	{
		uint64 hash = cityhash::cityhash64(str.data(), str.size());

		std::unique_lock lock(m_mutex);
		if (m_map.find(hash) == m_map.end())
		{
			m_map[hash] = allocate(str);
		}
		return hash;
	}

	StringTable::StringArena::StringArena(const uint32 capacity)
		: totalSize(capacity)
		, usedNum(0u)
	{
		memory = reinterpret_cast<char*>(std::malloc(capacity));
	}

	StringTable::StringArena::~StringArena()
	{
		std::free(memory);
	}

	std::string_view StringTable::allocate(std::string_view str)
	{
		uint32 newStringSize = str.size();
		assert(newStringSize <= 1024);

		// 7 > 1024;
		// 6 >  512;
		// 5 >  256;
		// 4 >  128;
		// 3 >   64;
		// 2 >   32;
		// 1 >   16;
		// 0 >    8; 
		constexpr int32 kBasePowOf2 = 3; // 2^3 = 8

		// Round up log2
		int32 baseId = math::max(0, int32(math::log2(getNextPOT(newStringSize))) - kBasePowOf2);
		while (true)
		{
			if (m_freeStringArena[baseId].empty())
			{
				baseId ++;
				if (baseId >= kMaxArenaFreeSplit)
				{
					break; // No free arena can used.
				}
			}
			else
			{
				auto& queue = m_freeStringArena[baseId];
				auto* arena = queue.front();
				queue.pop();

				//
				assert(arena->usedNum + newStringSize <= arena->totalSize);
				char* destData = &arena->memory[arena->usedNum];
				std::memcpy(destData, str.data(), str.size());
				arena->usedNum += newStringSize;

				// Update free size and try enqueue queue.
				uint32 freeSize = arena->totalSize - arena->usedNum;

				// Floor down log2
				int32 freeBaseId = math::clamp(int32(math::log2(freeSize)) - kBasePowOf2, 0, kMaxArenaFreeSplit - 1);
				if (freeBaseId < 0 && freeSize > 0)
				{
					m_garbageMemorySize += freeSize;
				}

				while (freeBaseId >= 0)
				{
					m_freeStringArena[freeBaseId].push(arena);
					freeBaseId --;
				}

				m_validUsedMemory += str.size();
				return std::string_view(destData, str.size());
			}
		}

		// Need create new arena.
		m_stringArena.push_back(std::make_unique<StringArena>(kMaxArenaStringSize));
		for (auto& queue : m_freeStringArena)
		{
			queue.push(m_stringArena.back().get());
		}
		return allocate(str);
	}
}