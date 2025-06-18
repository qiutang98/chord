#pragma once
#include <tsl/robin_map.h>
#include <utils/utils.h>
#include <utils/cityhash.h>
#include <utils/allocator.h>

namespace chord
{
	// Like unreal FName implement, but just store in one big robin hood hash map.
	template<typename StringViewType>
	class TStringTable
	{
	private:
		mutable std::shared_mutex m_mutex;
		tsl::robin_map<uint64, StringViewType> m_map;

		// Grow string arena.
		struct StringArena : NonCopyable
		{
			const uint32 totalSize;
			uint32 usedNum;
			char* memory;

			explicit StringArena(const uint32 capacity)
				: totalSize(capacity)
				, usedNum(0u)
			{
				memory = reinterpret_cast<char*>(traceMalloc(capacity));
			}

			~StringArena()
			{
				traceFree(memory, totalSize);
			}
		};
		std::vector<std::unique_ptr<StringArena>> m_stringArena;

		static constexpr int32 kMaxArenaFreeSplit = 8;
		std::queue<StringArena*> m_freeStringArena[kMaxArenaFreeSplit];

		// Default 64kb per page.
		static constexpr uint32 kMaxArenaStringSize = 1024 * 64; // 
		std::atomic<uint32> m_validUsedMemory = 0;
		std::atomic<uint32> m_garbageMemorySize = 0;

		//
		StringViewType allocate(StringViewType str)
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
					baseId++;
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
					int32 freeBaseId = math::min(int32(math::log2(freeSize)) - kBasePowOf2, kMaxArenaFreeSplit - 1);
					if (freeBaseId < 0 && freeSize > 0)
					{
						m_garbageMemorySize += freeSize;
					}
					else
					{
						while (freeBaseId >= 0)
						{
							m_freeStringArena[freeBaseId].push(arena);
							freeBaseId--;
						}
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

	public:
		TStringTable() = default;

		uint64 update(StringViewType str)
		{
			uint64 hash = cityhash::cityhash64(str.data(), str.size());

			std::unique_lock lock(m_mutex);
			if (m_map.find(hash) == m_map.end())
			{
				m_map[hash] = allocate(str);
			}
			return hash;
		}

		StringViewType getString(uint64 hash) const
		{
			std::shared_lock lock(m_mutex);
			return m_map.at(hash);
		}

		size_t getUsedMemory() const
		{
			return m_stringArena.size() * kMaxArenaStringSize;
		}

		float getMemoryUsagePercentage() const
		{
			if (m_stringArena.empty())
			{
				return 1.0f;
			}

			return double(m_validUsedMemory) / double(getUsedMemory());
		}

		uint32 getGarbageMemorySize() const
		{
			return m_garbageMemorySize;
		}
	};


	template<typename StringViewType, typename CharStringType>
	class TFName
	{
	private:
		struct TaggedPtr
		{
			uintptr_t ptr : 48;
			uintptr_t tag : 16;
		};

		union 
		{
			uint64 m_hashId;
			TaggedPtr m_taggerPtr;
		};

		static TStringTable<StringViewType> sFNameTable;
		
		static constexpr uintptr_t kRodataTag = 39;
		static constexpr uintptr_t kUnvalidId = ~0;
	public:
		TFName(StringViewType str, bool bRodata)
		{
			if (bRodata)
			{
				m_taggerPtr.tag = kRodataTag;
				m_taggerPtr.ptr = reinterpret_cast<uintptr_t>(str.data());
				assert(reinterpret_cast<CharStringType>(m_taggerPtr.ptr) == str.data());
			}
			else
			{
				m_hashId = sFNameTable.update(str);
			}
		}

		TFName()
			: m_hashId(kUnvalidId)
		{

		}

		TFName(const TFName& other)
			: m_hashId(other.m_hashId)
		{

		}

		TFName& operator=(const TFName& other)
		{
			this->m_hashId = other.m_hashId;
			return *this;
		}

		bool isRodata() const
		{
			return m_taggerPtr.tag == kRodataTag;
		}

		inline bool isValid() const
		{
			return m_hashId != kUnvalidId;
		}

		uint64 getHashId() const
		{
			return m_hashId;
		}

		StringViewType string_view() const
		{
			if (isValid())
			{
				return "";
			}
			return isRodata() ? reinterpret_cast<CharStringType>(m_taggerPtr.ptr) : sFNameTable.getString(m_hashId);
		}

		inline bool operator==(const TFName& rhs) const
		{ 
			return rhs.m_hashId == m_hashId; 
		}
	};

	using FName = TFName<std::string_view, const char*>;
	using FNameU16 = TFName<std::u16string_view, const char16_t*>;
}