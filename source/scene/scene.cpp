#include <scene/scene.h>
#include <ui/ui_helper.h>
#include <asset/asset.h>
#include <asset/asset_common.h>
#include <asset/serialize.h>



namespace chord
{
	AssetTypeMeta Scene::createTypeMeta()
	{
		AssetTypeMeta result;

		// 
		result.name = "Scene";
		result.icon = ICON_FA_CHESS_KING;
		result.decoratedName = std::string("  ") + ICON_FA_CHESS_KING + "    Scene";

		//
		result.suffix = ".assetscene";

		// Import config.
		{
			result.importConfig.bImportable = false;
		}

		return result;
	};

	const AssetTypeMeta Scene::kAssetTypeMeta = Scene::createTypeMeta();

	Scene::Scene(const AssetSaveInfo& saveInfo)
		: IAsset(saveInfo)
	{

	}

	bool Scene::onSave()
	{
		std::shared_ptr<IAsset> asset = ptr<Scene>();
		return saveAsset(asset, ECompressionMode::Lz4, m_saveInfo.path(), false);
	}

	void Scene::onUnload()
	{

	}

	void Scene::onPostConstruct()
	{
		if (m_atmosphereManager == nullptr)
		{
			m_atmosphereManager = std::make_unique<AtmosphereManager>();
		}

		if (m_shadowManager == nullptr)
		{
			m_shadowManager = std::make_unique<ShadowManager>();
		}

		if (m_ddgiManager == nullptr)
		{
			m_ddgiManager = std::make_unique<DDGIManager>();
		}

		// Construct root node post construct.
		if (m_root == nullptr)
		{
			m_root = createNode(kRootId, u16str("Root"));
		}
	}

	void Scene::postLoad()
	{
		// All node tick.
		loopNodeTopToDown([](SceneNodeRef node)
		{
			node->postLoad();
		}, m_root);
	}

	SceneNodeRef Scene::createNode(size_t id, const u16str& name)
	{
		auto node = SceneNode::create(id, name, ptr<Scene>());

		// Ensure no repeated node.
		check(!m_sceneNodes[node->getId()].lock());

		// Registered in map.
		m_sceneNodes[node->getId()] = node;
		return node;
	}

	size_t Scene::requireSceneNodeId()
	{
		checkMsgf(m_currentId < std::numeric_limits<size_t>::max(), "GUID max than max object id value.");

		m_currentId++;
		return m_currentId;
	}

	void Scene::tick(const ApplicationTickData& tickData)
	{
		m_atmosphereManager->update(tickData);

		// All node tick.
		loopNodeTopToDown([tickData](SceneNodeRef node)
		{
			node->tick(tickData);
		}, m_root);
	}

	void Scene::perviewPerframeCollect(PerframeCollected& collector, const PerframeCameraView& cameraView, const ICamera* camera)
	{
		loopNodeTopToDown([&](SceneNodeRef node)
		{
			node->perviewPerframeCollect(collector, cameraView, camera);
		}, m_root);
	}

	void Scene::deleteNode(SceneNodeRef node)
	{
		// Delete node will erase node loop from top to down.
		loopNodeTopToDown(
			[&](SceneNodeRef nodeLoop)
			{
				m_sceneNodes.erase(nodeLoop->getId());
			},
			node);

		// Cancel node's parent relationship.
		node->unparent();

		markDirty();
	}

	SceneNodeRef Scene::createNode(const u16str& name, SceneNodeRef parent)
	{
		// Use require id to avoid guid repeat problem.
		auto result = createNode(requireSceneNodeId(), name);

		// Set node parent relationship.
		setParent(parent ? parent : m_root, result);

		// Mark whole scene asset dirty.
		markDirty();

		// Now return new node.
		return result;
	}

	void Scene::addChild(SceneNodeRef child)
	{
		m_root->addChild(child);
	}

	void Scene::loopNodeDownToTop(
		const std::function<void(SceneNodeRef)>& func,
		SceneNodeRef node)
	{
		auto& children = node->getChildren();
		for (auto& child : children)
		{
			loopNodeDownToTop(func, child);
		}

		func(node);
	}

	void Scene::loopNodeTopToDown(
		const std::function<void(SceneNodeRef)>& func,
		SceneNodeRef node)
	{
		func(node);

		auto& children = node->getChildren();
		for (auto& child : children)
		{
			loopNodeTopToDown(func, child);
		}
	}

	SceneNodeRef Scene::findNode(const u16str& name) const
	{
		if (name == m_root->getName())
		{
			return m_root;
		}

		for (auto& rootChild : m_root->getChildren())
		{
			std::queue<SceneNodeRef> traverseNodes{};
			traverseNodes.push(rootChild);

			while (!traverseNodes.empty())
			{
				auto& node = traverseNodes.front();
				traverseNodes.pop();

				if (node->getName() == name)
				{
					return node;
				}

				for (auto& childNode : node->getChildren())
				{
					traverseNodes.push(childNode);
				}
			}
		}

		return nullptr;
	}

	std::vector<SceneNodeRef> Scene::findNodes(const u16str& name) const
	{
		std::vector<SceneNodeRef> results{ };

		for (auto& rootChild : m_root->getChildren())
		{
			std::queue<SceneNodeRef> traverseNodes{};
			traverseNodes.push(rootChild);

			while (!traverseNodes.empty())
			{
				auto& node = traverseNodes.front();
				traverseNodes.pop();

				if (node->getName() == name)
				{
					results.push_back(node);
				}

				for (auto& childNode : node->getChildren())
				{
					traverseNodes.push(childNode);
				}
			}
		}

		if (name == m_root->getName())
		{
			results.push_back(m_root);
		}

		return results;
	}

	bool Scene::setParent(SceneNodeRef parent, SceneNodeRef son)
	{
		bool bNeedSet = false;

		if (parent == son)
		{
			return false;
		}

		auto oldP = son->getParent();

		if (oldP == nullptr || (!son->isSon(parent) && parent->getId() != oldP->getId()))
		{
			bNeedSet = true;
		}

		if (bNeedSet)
		{
			son->setParent(parent);
			return true;
		}

		return false;
	}

	// Sync scene node tree's transform form top to down to get current result.
	void Scene::flushSceneNodeTransform()
	{
		loopNodeTopToDown([](SceneNodeRef node)
			{
				node->getTransform()->updateWorldTransform();
			}, m_root);
	}

	bool Scene::existNode(size_t id) const
	{
		return m_sceneNodes.contains(id);
	}

	SceneNodeRef Scene::getNode(size_t id) const
	{
		return m_sceneNodes.at(id).lock();
	}

	bool Scene::removeComponent(SceneNodeRef node, const std::string& type)
	{
		if (node->hasComponent(type))
		{
			node->removeComponent(type);

			markDirty();
			return true;
		}

		return false;
	}
}