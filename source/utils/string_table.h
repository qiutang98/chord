#pragma once
#include <tsl/robin_map.h>
#include <utils/utils.h>
#include <utils/cityhash.h>

namespace chord
{
	// Like unreal FName implement, but just store in one big robin hood hash map.
	class StringTable
	{
	private:
		mutable std::shared_mutex m_mutex;
		tsl::robin_map<uint64, std::string_view> m_map;

		StringTable() = default;

		// Grow string arena.
		struct StringArena : NonCopyable
		{
			const uint32 totalSize;
			uint32 usedNum;
			char* memory;

			explicit StringArena(const uint32 capacity);
			~StringArena();
		};
		std::vector<std::unique_ptr<StringArena>> m_stringArena;

		static constexpr int32 kMaxArenaFreeSplit = 8;
		std::queue<StringArena*> m_freeStringArena[kMaxArenaFreeSplit];

		// Default 64kb per page.
		static constexpr uint32 kMaxArenaStringSize = 1024 * 64; // 
		std::atomic<uint32> m_validUsedMemory = 0;
		std::atomic<uint32> m_garbageMemorySize = 0;

		//
		std::string_view allocate(std::string_view str);

	public:
		static StringTable& get();

		uint64 update(std::string_view str);

		std::string_view getString(uint64 hash) const
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

	class FName
	{
	private:
		uint64 m_hashId;

	public:
		FName(std::string_view str)
			: m_hashId(StringTable::get().update(str))
		{

		}

		FName()
			: m_hashId(~0)
		{

		}

		FName(const FName& other)
			: m_hashId(other.m_hashId)
		{

		}

		FName& operator=(const FName& other)
		{
			this->m_hashId = other.m_hashId;
			return *this;
		}

		FName& operator=(std::string_view str)
		{
			*this = FName(str);
			return *this;
		}

		inline bool isValid() const
		{
			return m_hashId != ~0;
		}

		inline bool operator==(const FName& rhs) const
		{ 
			return rhs.m_hashId == m_hashId; 
		}
	};
}