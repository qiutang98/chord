#pragma once
#include <utils/utils.h>

namespace chord
{
    template<typename T, class SizeT = uint64>
    class intrusive_ptr_counter : public T
    {
    private:
        std::atomic<SizeT> m_counter { 1 };

    public:
        void intrusive_ptr_counter_addRef()
        {
            m_counter++;
        }

        void intrusive_ptr_counter_release()
        {
            SizeT result = --m_counter;
            if (result == 0)
            {
                delete this;
            }
        }
    };

    template <typename T>
    class intrusive_ptr final
    {
    protected:
        T* m_ptr { nullptr };
        template<class OtherType> friend class intrusive_ptr;

        void internalAddRef() const 
        {
            if (m_ptr != nullptr)
            {
                m_ptr->intrusive_ptr_counter_addRef();
            }
        }

        void internalRelease() 
        {
            if (m_ptr != nullptr)
            {
                m_ptr->intrusive_ptr_counter_release();
                m_ptr = nullptr;
            }
        }

    public:
        intrusive_ptr() = default;

        intrusive_ptr(std::nullptr_t)  : m_ptr(nullptr)
        {
        }

        template<class OtherType>
        intrusive_ptr(OtherType* other)  : m_ptr(other)
        {
            internalAddRef();
        }

        intrusive_ptr(const intrusive_ptr& other)  : m_ptr(other.m_ptr)
        {
            internalAddRef();
        }

        template<class OtherType>
        intrusive_ptr(const intrusive_ptr<OtherType>& other)  :
            m_ptr(other.m_ptr)
        {
            static_assert(std::is_convertible_v<OtherType*, T*>);
            internalAddRef();
        }

        intrusive_ptr(intrusive_ptr&& other)  : m_ptr(nullptr)
        {
            if (this != reinterpret_cast<intrusive_ptr*>(&reinterpret_cast<unsigned char&>(other)))
            {
                swap(other);
            }
        }

        template<class OtherType>
        intrusive_ptr(intrusive_ptr<OtherType>&& other)  :
            m_ptr(other.m_ptr)
        {
            static_assert(std::is_convertible_v<OtherType*, T*>);
            other.m_ptr = nullptr;
        }

        ~intrusive_ptr() 
        {
            internalRelease();
        }

        intrusive_ptr& operator=(std::nullptr_t) 
        {
            internalRelease();
            return *this;
        }

        intrusive_ptr& operator=(T* other) 
        {
            if (m_ptr != other)
            {
                intrusive_ptr(other).swap(*this);
            }
            return *this;
        }

        template <typename OtherType>
        intrusive_ptr& operator=(OtherType* other) 
        {
            intrusive_ptr(other).swap(*this);
            return *this;
        }

        intrusive_ptr& operator=(const intrusive_ptr& other) 
        {
            if (m_ptr != other.m_ptr)
            {
                intrusive_ptr(other).swap(*this);
            }
            return *this;
        }

        template<class OtherType>
        intrusive_ptr& operator=(const intrusive_ptr<OtherType>& other) 
        {
            intrusive_ptr(other).swap(*this);
            return *this;
        }

        intrusive_ptr& operator=(intrusive_ptr&& other) 
        {
            intrusive_ptr(static_cast<intrusive_ptr&&>(other)).swap(*this);
            return *this;
        }

        template<class OtherType>
        intrusive_ptr& operator=(intrusive_ptr<OtherType>&& other) 
        {
            intrusive_ptr(static_cast<intrusive_ptr<OtherType>&&>(other)).swap(*this);
            return *this;
        }

        void swap(intrusive_ptr&& r) 
        {
            std::swap(m_ptr, r.m_ptr);
        }

        void swap(intrusive_ptr& r) 
        {
            std::swap(m_ptr, r.m_ptr);
        }

        T* get() const 
        {
            return m_ptr;
        }

        operator T*() const
        {
            return m_ptr;
        }

        T* operator->() const 
        {
            return m_ptr;
        }

        T** operator&()
        {
            return &m_ptr;
        }

        void reset()
        {
            internalRelease();
        }

        void attach(T* other)
        {
            if (m_ptr != nullptr)
            {
                m_ptr->intrusive_ptr_counter_release();
            }
            m_ptr = other;
        }

        template<class...Args>
        static intrusive_ptr<T> create(Args&&...args)
        {
            intrusive_ptr<T> ptr;
            ptr.attach(new T(std::forward<Args>(args)...));
            return ptr;
        }
    };
}