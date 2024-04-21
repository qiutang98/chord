#pragma once
#include <utils/utils.h>

namespace chord
{
	template<typename ValueType, typename KeyType>
	class LRUCache : NonCopyable
	{
	public:
		static_assert(requires(const ValueType& t) { { t.getSize() } -> std::convertible_to<std::size_t>; });

		// Init lru asset cache with capacity and elasticity in MB unit.
		explicit LRUCache(const std::string& name, size_t capacity, size_t elasticity)
			: m_capacity(capacity * 1024 * 1024)
			, m_elasticity(elasticity * 1024 * 1024)
			, m_name(name)
		{
			LOG_TRACE("LRU cache '{0}' construct with {1} MB capacity and {2} MB elasticity.", m_name, capacity, elasticity);
		}

		~LRUCache()
		{
			clear();
		}

		size_t getCapacity() const 
		{
			return m_capacity; 
		}

		size_t getElasticity() const 
		{ 
			return m_elasticity; 
		}

		size_t getMaxAllowedSize() const 
		{
			return m_capacity + m_elasticity; 
		}

		// Current LRU owner shared_ptr map cache used size, no included asset owner by other actor.
		size_t getUsedSize() const 
		{ 
			return m_usedSize.load(); 
		}

		// Is contain current asset key.
		CHORD_NODISCARD bool contain(const KeyType& key)
		{
			// Need lock when search from weak ptr map.
			std::lock_guard<std::mutex> lockGuard(m_lock);
			return m_lruMap.contains(key);
		}

		// Clear all lru cache.
		void clear()
		{
			std::lock_guard<std::mutex> lockGuard(m_lock);
			
			// Make used size as zero.
			m_usedSize = 0;

			// LRU cache clear.
			m_lruMap.clear();
			m_lruList.clear();
		}

		// Insert one, return prune size.
		void insert(const KeyType& key, std::shared_ptr<ValueType> value)
		{
			std::lock_guard<std::mutex> lockGuard(m_lock);

			// Add size first.
			m_usedSize += value->getSize();

			// LRU asset can insert repeatly, it loop whole map to find exist or not.
			const auto iter = m_lruMap.find(key);
			if (iter != m_lruMap.end())
			{
				auto& listNode = iter->second;

				// Key exist, update map value, and update list.
				m_usedSize -= listNode->second->getSize();
				listNode->second = value;
				check(listNode->first == key);

				m_lruList.splice(m_lruList.begin(), m_lruList, listNode);
				return;
			}

			// Key no exist, emplace to list front, and update map key-value.
			m_lruList.emplace_front(key, value);
			m_lruMap[key] = m_lruList.begin();

			// May oversize, need reduce.
			prune();
		}

		// Try to get value, will return nullptr if no exist.
		std::shared_ptr<ValueType> tryGet(const KeyType& inKey)
		{
			std::lock_guard<std::mutex> lockGuard(m_lock);

			// Else found in lru map.
			const auto iter = m_lruMap.find(inKey);
			if (iter == m_lruMap.end())
			{
				// No valid instance, return nullptr and need reload.
				return nullptr;
			}

			// Still exist in lru map, set as first guy.
			m_lruList.splice(m_lruList.begin(), m_lruList, iter->second);
			return iter->second->second;
		}

	protected:
		// Prune lru map.
		void prune()
		{
			size_t maxAllowed = m_capacity + m_elasticity;
			if (m_capacity == 0 || m_usedSize < maxAllowed)
			{
				return;
			}

			// Loop until release enough resource.
			size_t reduceSize = 0;
			while (m_usedSize > m_capacity)
			{
				size_t eleSize = m_lruList.back().second->getSize();

				// Erase last one.
				m_lruMap.erase(m_lruList.back().first);
				m_lruList.pop_back();

				// Update size.
				m_usedSize -= eleSize;
				reduceSize += eleSize;
			}

			LOG_TRACE("LRU cache '{0}' reduce size {1} KB.", m_name, float(reduceSize) / 1024.0f);
		}

	protected:
		using LRUList = std::list<std::pair<KeyType, std::shared_ptr<ValueType>>>;
		using LRUListNode = LRUList::iterator;

		// LRU data struct.
		LRUList m_lruList;
		std::unordered_map<KeyType, LRUListNode> m_lruMap;

		// LRU cache desire capacity.
		size_t m_capacity;

		// Some elasticity space to enable LRU cache no always prune.
		size_t m_elasticity;

		// Shared_ptr(m_lruMap) used size.
		std::atomic<size_t> m_usedSize = 0;

		// Lock mutex for map edit.
		std::mutex m_lock;

		std::string m_name;
	};
}