#pragma once

#include <scene/component.h>
#include <scene/scene_node.h>
#include <shader/base.h>

namespace chord
{

	// Simple scene graph implement.
	class Scene : public IAsset
	{
		REGISTER_BODY_DECLARE(IAsset);

		friend class AssetManager;
		friend SceneNode;
	public:
		Scene() = default;

		explicit Scene(const AssetSaveInfo& saveInfo);
		virtual ~Scene() = default;

		static const AssetTypeMeta kAssetTypeMeta;
	protected:
		// ~IAsset virtual function.
		// Call back when call AssetManager::createAsset
		virtual void onPostConstruct() override;
		virtual bool onSave() override;
		virtual void onUnload() override;
		// ~IAsset virtual function.

	public:
		// Tick every frame.
		void tick(const ApplicationTickData& tickData);

		// Get root node.
		SceneNodeRef getRootNode() const
		{
			return m_root;
		}

	public: 
		// Root node id.
		static const size_t kRootId = 0;

		// How many node exist.
		size_t getNodeCount() const 
		{ 
			return m_sceneNodes.size(); 
		}

		// Delete one node, also include it's child nodes.
		void deleteNode(SceneNodeRef node);

		// Create node.
		SceneNodeRef createNode(const u16str& name, SceneNodeRef parent = nullptr);

		// Add child for root node.
		void addChild(SceneNodeRef child);

		// Set node's parent relationship.
		bool setParent(SceneNodeRef parent, SceneNodeRef son);

		// Post-order loop.
		void loopNodeDownToTop(const std::function<void(SceneNodeRef)>& func, SceneNodeRef node);

		// Pre-order loop.
		void loopNodeTopToDown(const std::function<void(SceneNodeRef)>& func, SceneNodeRef node);

		// Loop the whole graph to find first same name scene node, this is a slow function.
		SceneNodeRef findNode(const u16str& name) const;

		// Find all same name nodes, this is a slow function.
		std::vector<SceneNodeRef> findNodes(const u16str& name) const;

		// update whole graph's transform.
		void flushSceneNodeTransform();

		// Node is exist or not.
		bool existNode(size_t id) const;

		// Get node with check.
		SceneNodeRef getNode(size_t id) const;

	protected:
		// require guid of scene node in this scene.
		size_t requireSceneNodeId();

		// Create scene node.
		SceneNodeRef createNode(size_t id, const u16str& name);

	public:
		// ~Component operator

		// Get components.
		inline const std::vector<ComponentWeak>& getComponents(const std::string& id) const
		{
			return m_components.at(id);
		}

		template <typename T>
		inline const std::vector<ComponentWeak>& getComponents() const
		{
			return getComponents(typeid(T).name());
		}

		template <typename T>
		inline std::vector<ComponentWeak>& getComponents()
		{
			return m_components.at(typeid(T).name());
		}

		// Check exist component or not.
		inline bool hasComponent(const std::string& id) const
		{
			return m_components.contains(id);
		}

		// Loop scene's components.
		template<typename T> 
		void loopComponents(std::function<bool(std::shared_ptr<T>)>&& func);

		// Add component for node.
		template<typename T> 
		bool addComponent(std::shared_ptr<T> component, SceneNodeRef node);
		bool addComponent(const std::string& type, ComponentRef component, SceneNodeRef node);

		template <typename T>
		bool hasComponent() const
		{
			static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
			return hasComponent(typeid(T).name());
		}

		template<typename T>
		bool removeComponent(SceneNodeRef node)
		{
			static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
			return removeComponent(typeid(T).name());
		}

		bool removeComponent(SceneNodeRef node, const std::string& type);

	private:
		static AssetTypeMeta createTypeMeta();

		// Perframe data collected from every components.
		PerframeCollected m_perframe;

	private:
		// Cache scene node index. use for runtime guid.
		size_t m_currentId = kRootId;

		// Owner of the root node.
		SceneNodeRef m_root = nullptr;

		// Cache scene components, no include transform.
		std::map<std::string, std::vector<ComponentWeak>> m_components;

		// Cache scene node maps.
		std::map<size_t, SceneNodeWeak> m_sceneNodes;
	};


	template<typename T>
	inline void Scene::loopComponents(std::function<bool(std::shared_ptr<T>)>&& func)
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
		if (!hasComponent<T>())
		{
			return;
		}
		
		auto& compWeakPtrs = getComponents<T>();

		// Loop all component.
		size_t unvalidComponentNum = 0U;
		for (auto& p : compWeakPtrs)
		{
			if (auto pShared = p.lock())
			{
				// Some function require pre-return after find first component.
				if (func(std::static_pointer_cast<T>(pShared)))
				{
					return;
				}
			}
			else
			{
				unvalidComponentNum++;
			}
		}

		// Try shrink scene components if need.
		static const size_t kShrinkComponentNumMin = 10U;
		if (unvalidComponentNum > kShrinkComponentNumMin)
		{
			compWeakPtrs.erase(std::remove_if(compWeakPtrs.begin(), compWeakPtrs.end(),
				[](const ComponentWeak& p)
				{
					return !p.lock();
				}),
				compWeakPtrs.end());
		}
	}

	inline bool Scene::addComponent(
		const std::string& type, 
		ComponentRef component, 
		SceneNodeRef node)
	{
		if (component && !node->hasComponent(type))
		{
			node->setComponent(type, component);
			m_components[type].push_back(component);
			markDirty();

			return true;
		}
		return false;
	}

	template<typename T>
	inline bool Scene::addComponent(std::shared_ptr<T> component, SceneNodeRef node)
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
		return addComponent(typeid(T).name(), component, node);
	}



}