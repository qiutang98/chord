#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/delegate.h>
#include <utils/thread.h>
#include <tsl/robin_map.h>
#include <utils/cityhash.h>

namespace chord
{
	enum class EConsoleVarFlags : uint16
	{
		None = 0,
		
		// ReadOnly meaning can not change value by code in runtime.
		ReadOnly    = 0x01 << 0,

		// Export this value as a scalability config.
		Scalability = 0x01 << 1, 
		
		MAX,
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

		EConsoleVarFlags getFlags() const { return m_flags; }
		std::string_view getName() const { return m_name; }
		std::string_view getDescription() const { return m_description; }

		virtual bool isValueTypeMatch(const char* typeName) const = 0;
	
	private:
		EConsoleVarFlags m_flags;
		std::string_view m_name;
		std::string_view m_description;
	};

	template <typename T>
	class CVarStorageInterface : public CVarStorage
	{
	public:
		explicit CVarStorageInterface(EConsoleVarFlags flag, std::string_view name, std::string_view description)
			: CVarStorage(flag, name, description)
		{

		}

		// Type match or not.
		virtual bool isValueTypeMatch(const char* typeName) const override
		{
			return typeName == getTypeName<T>();
		}

		// 
		const T& get() const 
		{ 
			return getImpl(); 
		}

		// 
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

		// Callback when data change.
		Delegate<void, const T&, T&> onValueChange { };

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

		// Callback when data change.
		Delegate<void, const T& /*oldValue*/, T& /*storage*/> onValueChange{ };

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
			if (onValueChange.isBound())
			{
				const T oldData = m_reference;
				m_reference = v;

				onValueChange.execute(oldData, m_reference);
			}
			else
			{
				m_reference = v;
			}
		}

	private:
		const T m_defaultValue;
		T& m_reference;
	};

	template<class T>
	constexpr void cVarDisableTypeCheck()
	{
		constexpr bool bTypeValid =
			std::is_same_v<T, uint32> || 
			std::is_same_v<T,  int32> ||
			std::is_same_v<T,   bool> ||
			std::is_same_v<T,  float> ||
			std::is_same_v<T, double> ||
			std::is_same_v<T, u16str>;

		static_assert(bTypeValid, "Don't use un-support type when create cvar.");
	}

	class CVarSystem final : NonCopyable
	{
		template <typename T, typename Lambda> friend class AutoCVar;
		template <typename T, typename Lambda> friend class AutoCVarRef;
	public:
		static CVarSystem& get();

		CVarStorage* getCVarIfExistGeneric(std::string_view name) const
		{
			const size_t hashId = cityhash::cityhash64(name.data(), name.size());
			if (!m_storages.contains(hashId))
			{
				return nullptr;
			}

			return m_storages.at(hashId).get();
		}

		bool setValueIfExistGeneric(std::string_view name, const std::string& value);
		std::string getValueIfExistGeneric(std::string_view name);

		template <typename T>
		CVarStorageInterface<T>* getCVarIfExist(std::string_view name) const 
		{
			cVarDisableTypeCheck<T>();
			return (CVarStorageInterface<T>*)getCVarIfExistGeneric(name);
		}

		template <typename T>
		CVarStorageInterface<T>* getCVarCheck(std::string_view name) const
		{
			auto* ptr = getCVarIfExist<T>(name);
			assert(ptr != nullptr);

			return ptr;
		}

		struct CacheCommand
		{
			std::string_view name;
			CVarStorage* storage;
		};

		const auto& getCacheCommands() const
		{
			return m_cacheCommands;
		}

	private:
		static constexpr size_t kCacheCommandsReserveSize = 1024;
		CVarSystem()
		{
			m_cacheCommands.reserve(kCacheCommandsReserveSize);
		}

		void addCacheCommand(CacheCommand&& cmd);

		template <typename T>
		CVarStorageValue<T>* addCVar(EConsoleVarFlags flag, std::string_view name, std::string_view description, const T& v)
		{
			cVarDisableTypeCheck<T>();

			std::lock_guard lock(m_mutex);

			const size_t hashId = cityhash::cityhash64(name.data(), name.size());
			assert(!m_storages.contains(hashId));

			m_storages[hashId] = std::make_unique<CVarStorageValue<T>>(flag, name, description, v);

			{
				CacheCommand cacheCommand;
				cacheCommand.name = m_storages[hashId]->getName();
				cacheCommand.storage = m_storages[hashId].get();
				addCacheCommand(std::move(cacheCommand));
			}


			return (CVarStorageValue<T>*)m_storages[hashId].get();
		}

		template <typename T>
		CVarStorageRef<T>* addCVarRef(EConsoleVarFlags flag, std::string_view name, std::string_view description, T& v)
		{
			cVarDisableTypeCheck<T>();

			std::lock_guard lock(m_mutex);

			const size_t hashId = cityhash::cityhash64(name.data(), name.size());
			assert(!m_storages.contains(hashId));

			m_storages[hashId] = std::make_unique<CVarStorageRef<T>>(flag, name, description, v);

			{
				CacheCommand cacheCommand;
				cacheCommand.name = m_storages[hashId]->getName();
				cacheCommand.storage = m_storages[hashId].get();
				addCacheCommand(std::move(cacheCommand));
			}

			return (CVarStorageRef<T>*)m_storages[hashId].get();
		}

	private:
		mutable std::mutex m_mutex;
		tsl::robin_map<size_t, std::unique_ptr<CVarStorage>> m_storages;

		//
		std::vector<CacheCommand> m_cacheCommands;
	};

	template <typename T, typename Lambda = decltype([](const T&, T&) {})>
	class AutoCVar
	{
	public:
		explicit AutoCVar(std::string_view name, const T& v,  std::string_view description, EConsoleVarFlags flag = EConsoleVarFlags::None)
		{
			cVarDisableTypeCheck<T>();
			m_ptr = CVarSystem::get().addCVar<T>(flag, name, description, v);
		}

		explicit AutoCVar(std::string_view name, const T& v, std::string_view description, EConsoleVarFlags flag, Lambda&& onValueChangeCallback) 
			: AutoCVar(name, v, description, flag)
		{
			m_ptr->onValueChange.bind(std::move(onValueChangeCallback));
		}

		const T& get() const { return m_ptr->get(); }
		void set(const T& v) { m_ptr->set(v); }
		void reset() { m_ptr->reset(); }

		const auto* getPtr() const { return m_ptr; }

	private:
		CVarStorageValue<T>* m_ptr;
	};

	template <typename T, typename Lambda = decltype([](const T&, T&) {})>
	class AutoCVarRef
	{
	public:
		explicit AutoCVarRef(std::string_view name, T& v, std::string_view description, EConsoleVarFlags flag = EConsoleVarFlags::None)
		{
			cVarDisableTypeCheck<T>();
			m_ptr = CVarSystem::get().addCVarRef<T>(flag, name, description, v);
		}

		explicit AutoCVarRef(std::string_view name, T& v, std::string_view description, EConsoleVarFlags flag, Lambda&& onValueChangeCallback)
			: AutoCVarRef(name, v, description, flag)
		{
			m_ptr->onValueChange.bind(std::move(onValueChangeCallback));
		}

		const T& get() const { return m_ptr->get(); }
		void set(const T& v) { m_ptr->set(v); }
		void reset() { m_ptr->reset(); }

		const auto* getPtr() const { return m_ptr; }

	private:
		CVarStorageRef<T>* m_ptr;
	};

	// Cmd must name start with "cmd.", and type always is bool type.

}