#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/delegate.h>
#include <utils/log.h>
#include <mutex>
#include <shared_mutex>

namespace chord
{
	enum class EConsoleVarFlags : uint16
	{
		NONE = 0,
		
		// ReadOnly meaning can not change value by code in runtime.
		ReadOnly    = 0x01 << 0,

		// Export this value as a scalability config.
		Scalability = 0x01 << 1, 
		
		COUNT,
	};
	ENUM_CLASS_FLAG_OPERATORS(EConsoleVarFlags)

	class CVarStorage
	{
	public:
		explicit CVarStorage(EConsoleVarFlags flag, std::string_view name, std::string_view description)
			: m_flags(flag)
			, m_name(name)
			, m_description(description)
		{

		}

		virtual ~CVarStorage() = default;

		EConsoleVarFlags getFlags() const { return m_flags; }
		std::string_view getName() const { return m_name; }
		std::string_view getDescription() const { return m_description; }
	
	private:
		EConsoleVarFlags m_flags;
		std::string m_name;
		std::string m_description;
	};

	template <typename T>
	class CVarStorageInterface : public CVarStorage
	{
	public:
		explicit CVarStorageInterface(EConsoleVarFlags flag, std::string_view name, std::string_view description)
			: CVarStorage(flag, name, description)
		{

		}

		virtual ~CVarStorageInterface() = default;

		const T& get() const 
		{ 
			return getImpl(); 
		}

		void set(const T& v)
		{
			// Just return if no data change.
			if (getImpl() == v)
			{
				return;
			}

			setImpl(v);
		}

		void reset()
		{
			if (getImpl() == getDefault())
			{
				return;
			}
			setImpl(getDefault());
		}

	protected:
		// Interface of get value.
		virtual const T& getImpl() const = 0;

		// Interface of get default value.
		virtual const T& getDefault() const = 0;

		// Interface of set value.
		virtual void setImpl(const T& v) = 0;
	};

	template <typename T>
	class CVarStorageValue final : public CVarStorageInterface<T>
	{
	public:
		explicit CVarStorageValue(EConsoleVarFlags flag, std::string_view name, std::string_view description, const T& v)
			: CVarStorageInterface<T>(flag, name, description)
			, m_defaultValue(v)
			, m_currentValue(v)
		{

		}

		virtual ~CVarStorageValue() = default;

		// Callback when data change.
		Delegate<CVarStorageValue<T>, void, const T&, const T&> onValueChange { };

	private:
		virtual const T& getImpl() const override 
		{ 
			return m_currentValue; 
		}

		virtual const T& getDefault() const override 
		{ 
			return m_defaultValue;
		}

		// Storage value can monitor value state so exist onValueChange callback.
		virtual void setImpl(const T& v) override 
		{ 
			if (onValueChange.isBound())
			{
				const T oldData = m_currentValue;
				m_currentValue = v;

				// Callback execute.
				onValueChange.execute(oldData, m_currentValue);
			}
			else
			{
				m_currentValue = v;
			}
		}

	private:
		const T m_defaultValue;
		T m_currentValue;
	};

	template <typename T>
	class CVarStorageRef final : public CVarStorageInterface<T>
	{
	public:
		explicit CVarStorageRef(EConsoleVarFlags flag, std::string_view name, std::string_view description, T& v)
			: CVarStorageInterface<T>(flag, name, description)
			, m_defaultValue(v)
			, m_reference(v)
		{

		}

		virtual ~CVarStorageRef() = default;

	private:
		virtual const T& getImpl() const override 
		{ 
			return m_reference; 
		}

		virtual const T& getDefault() const override 
		{ 
			return m_defaultValue; 
		}

		virtual void setImpl(const T& v) override 
		{
			m_reference = v; 
		}

	private:
		const T m_defaultValue;
		T& m_reference;
	};


	class CVarSystem final : NonCopyable
	{
		template <typename T> friend class AutoCVar;
		template <typename T> friend class AutoCVarRef;
	public:
		static CVarSystem& get();



		template <typename T>
		CVarStorageInterface<T>* getCVarIfExist(std::string_view name) const 
		{
			std::shared_lock<std::shared_mutex> lock(m_lock);

			const size_t hashId = std::hash<std::string_view>()(name);
			if (!m_storages.contains(hashId))
			{
				return nullptr;
			}

			return (CVarStorageInterface<T>*)m_storages.at(hashId).get();
		}

		template <typename T>
		CVarStorageInterface<T>* getCVarCheck(std::string_view name) const
		{
			auto* ptr = getCVarIfExist<T>(name);
			CHECK(ptr != nullptr);

			return ptr;
		}

	private:
		CVarSystem() = default;

		template <typename T>
		CVarStorageValue<T>* addCVar(EConsoleVarFlags flag, std::string_view name, std::string_view description, const T& v)
		{
			std::unique_lock<std::shared_mutex> lock(m_lock);

			const size_t hashId = std::hash<std::string_view>()(name);
			CHECK(!m_storages.contains(hashId));

			m_storages[hashId] = std::make_unique<CVarStorageValue<T>>(flag, name, description, v);
			return (CVarStorageValue<T>*)m_storages[hashId].get();
		}

		template <typename T>
		CVarStorageRef<T>* addCVarRef(EConsoleVarFlags flag, std::string_view name, std::string_view description, T& v)
		{
			std::unique_lock<std::shared_mutex> lock(m_lock);

			const size_t hashId = std::hash<std::string_view>()(name);
			CHECK(!m_storages.contains(hashId));

			m_storages[hashId] = std::make_unique<CVarStorageRef<T>>(flag, name, description, v);
			return (CVarStorageRef<T>*)m_storages[hashId].get();
		}

	private:
		mutable std::shared_mutex m_lock;
		std::unordered_map<size_t, std::unique_ptr<CVarStorage>> m_storages;
	};

	template <typename T>
	class AutoCVar
	{
	public:
		explicit AutoCVar(std::string_view name, const T& v,  std::string_view description, EConsoleVarFlags flag = EConsoleVarFlags::NONE,
			std::function<void(const T&, const T&)>&& onValueChangeCallback = nullptr)
		{
			m_ptr = CVarSystem::get().addCVar<T>(flag, name, description, v);
			m_ptr->onValueChange.bind(std::move(onValueChangeCallback));
		}

		const T& get() const { return m_ptr->get(); }
		void set(const T& v) { m_ptr->set(v); }
		void reset() { m_ptr->reset(); }

		const auto* getPtr() const { return m_ptr; }

	private:
		CVarStorageValue<T>* m_ptr;
	};

	template <typename T>
	class AutoCVarRef
	{
	public:
		explicit AutoCVarRef(std::string_view name, T& v, std::string_view description, EConsoleVarFlags flag = EConsoleVarFlags::NONE)
		{
			m_ptr = CVarSystem::get().addCVarRef<T>(flag, name, description, v);
		}

		const T& get() const { return m_ptr->get(); }
		void set(const T& v) { m_ptr->set(v); }
		void reset() { m_ptr->reset(); }

		const auto* getPtr() const { return m_ptr; }

	private:
		CVarStorageRef<T>* m_ptr;
	};
}