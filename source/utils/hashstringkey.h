#pragma once

#include <utils/utils.h>

namespace chord
{
	// Sizeof DefaultValue<T> == sizeof(T).
	template<typename T, T kDefaultValue>
	class DefaultValue
	{
	private:
		T m_value = kDefaultValue;

	public:
		DefaultValue() = default;
		DefaultValue(const T& value)
			: m_value(value)
		{

		}

		DefaultValue(T&& value)
			: m_value(std::move(value))
		{

		}

		DefaultValue& operator=(const T& value)
		{
			m_value = value;
			return *this;
		}

		DefaultValue& operator=(T&& value)
		{
			m_value = std::move(value);
			return *this;
		}

		bool isValid() const
		{
			return m_value != kDefaultValue;
		}

		const T& get() const
		{
			return m_value;
		}
	};
	using OptionalSizeT = DefaultValue<std::size_t, ~0>;
	using OptionalUint32 = DefaultValue<uint32, ~0>;

	class HashStringKey
	{
	private:
		std::size_t m_hashKey = ~0;
	#if CHORD_DEBUG
		std::string m_hashString { };
	#endif 

	public:
		HashStringKey() = default;

		HashStringKey(const char* inStr)
			: m_hashKey(std::hash<std::string_view>{}(inStr))
		#if CHORD_DEBUG
			, m_hashString(inStr)
		#endif
		{

		}

		HashStringKey(std::string_view inStr)
			: m_hashKey(std::hash<std::string_view>{}(inStr))
		#if CHORD_DEBUG
			, m_hashString(inStr)
		#endif
		{

		}

		bool isValid() const
		{
			return m_hashKey != ~0;
		}

		bool operator<(const HashStringKey& src) const
		{
			return (this->m_hashKey < src.m_hashKey);
		}

		const auto& getHashId() const 
		{ 
			return m_hashKey; 
		}

	#if CHORD_DEBUG
		const auto& getString() const 
		{ 
			return m_hashString; 
		}
	#endif	
	};
}

template<> struct std::hash<chord::HashStringKey>
{
	std::size_t operator()(const chord::HashStringKey& k) const
	{
		return k.getHashId();
	}
};