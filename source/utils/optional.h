#pragma once

#include <utils/utils.h>

namespace chord
{
	// Sizeof OptionalValue<T> == sizeof(T).
	template<typename T, T kDefaultValue>
	class OptionalValue
	{
	private:
		T m_value = kDefaultValue;

	public:
		OptionalValue() = default;
		OptionalValue(const T& value)
			: m_value(value)
		{

		}

		OptionalValue(T&& value)
			: m_value(std::move(value))
		{

		}

		OptionalValue& operator=(const T& value)
		{
			m_value = value;
			return *this;
		}

		OptionalValue& operator=(T&& value)
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

		T& get()
		{
			return m_value;
		}
	};
	using OptionalSizeT  = OptionalValue<std::size_t, ~0>;
	using OptionalUint32 = OptionalValue<uint32,      ~0>;

	using OptionalVkShaderModule = OptionalValue<VkShaderModule, VK_NULL_HANDLE>;
}