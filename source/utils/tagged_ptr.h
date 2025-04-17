#pragma once

#include <pch.h>

namespace chord
{
	template <class T>
	class TaggedPointer
	{
	private:
		uintptr_t m_ptr : 48;
		uintptr_t m_tag : 16;

	public:
		void set(T* ptr, uint16_t tag)
		{
			m_ptr = reinterpret_cast<uintptr_t>(ptr);
			m_tag = (uintptr_t)tag;
			assert(ptr == reinterpret_cast<T*>(m_ptr));
		}

		TaggedPointer()
		{
			m_ptr = 0;
			m_tag = 0;
		}

		TaggedPointer(T* ptr, uint16_t tag = 0)
		{
			set(ptr, tag);
		}

		T* getPointer() const
		{
			if (m_ptr == 0) { return nullptr; }
			return reinterpret_cast<T*>(m_ptr);
		}

		uint16_t getTag() const
		{
			return (uint16_t)m_tag;
		}

		void setTag(uint16_t tag)
		{
			T* ptr = getPointer();
			set(ptr, tag);
		}
	};
}

