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
		tsl::robin_map<uint64, std::string> m_map;

		StringTable() = default;

	public:
		static StringTable& get();

		uint64 update(const std::string& str)
		{
			uint64 hash = cityhash::cityhash64(str.data(), str.size());
			if (m_map.find(hash) == m_map.end())
			{
				m_map[hash] = str;
			}
			return hash;
		}

		uint64 update(std::string&& inStr)
		{
			std::string str = std::move(inStr);

			uint64 hash = cityhash::cityhash64(str.data(), str.size());
			if (m_map.find(hash) == m_map.end())
			{
				m_map[hash] = std::move(str);
			}
			return hash;
		}

		const std::string& getString(uint64 hash) const
		{
			return m_map.at(hash);
		}
	};

	class FName
	{
	private:
		uint64 m_hashId;

	public:
		explicit FName(const std::string& str)
			: m_hashId(StringTable::get().update(str))
		{

		}

		explicit FName(std::string&& str)
			: m_hashId(StringTable::get().update(std::move(str)))
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