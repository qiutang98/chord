#include <world/world.h>
#include <ui/ui_helper.h>
#include <asset/asset_common.h>
#include <asset/serialize.h>

namespace chord::ecs
{
	AssetTypeMeta World::createTypeMeta()
	{
		AssetTypeMeta result;

		// 
		result.name = "World";
		result.icon = ICON_FA_MAP;
		result.decoratedName = std::string("  ") + ICON_FA_MAP + "    World";

		//
		result.suffix = ".assetworld";

		// Import config.
		{
			result.importConfig.bImportable = false;
		}

		return result;
	};
	const AssetTypeMeta World::kAssetTypeMeta = World::createTypeMeta();

	World::World(const AssetSaveInfo& saveInfo)
		: IAsset(saveInfo)
	{

	}

	bool World::onSave()
	{
		std::shared_ptr<IAsset> asset = ptr<World>();
		return saveAsset(asset, ECompressionMode::Lz4, m_saveInfo.path(), false);
	}

	void World::onUnload()
	{

	}

	void World::postSubsystemLoad()
	{
	}

	void World::tick(const ApplicationTickData& tickData)
	{

	}

	entt::entity World::createEntity(
		const u16str& inName, 
		entt::entity parent,
		bool bStatic,
		bool bVisible)
	{
		entt::entity entity = m_registry.create();

		RelationshipComponent relationship{};
		relationship.parent = parent;

		m_registry.emplace<RelationshipComponent>(entity, std::move(relationship));

		if (bStatic)
		{
			m_registry.emplace<StaticTag>(entity);
		}

		if (bVisible)
		{
			m_registry.emplace<VisibleTag>(entity);
		}
		
		m_registry.emplace<NameComponent>(entity, NameComponent{ .name = inName });

		if (parent != entt::null)
		{
			check(setParentRelationship(parent, entity, false));
		}

	
		return entity;
	}

	// B is A's descendant ?
	bool World::isDescendant(entt::entity A, entt::entity B) const
	{
		while (true)
		{
			auto& pR = m_registry.get<RelationshipComponent>(B);
			if (pR.parent == entt::null)
			{
				return false;
			}

			if (pR.parent == A)
			{
				return true;
			}

			B = pR.parent;
		}

		return false;
	}

	bool World::setParentRelationship(entt::entity parent, entt::entity child, bool bCheckDescendant)
	{
		if (bCheckDescendant && isDescendant(parent, child))
		{
			return false;
		}

		auto& pR = m_registry.get<RelationshipComponent>(parent);
		auto& cR = m_registry.get<RelationshipComponent>(child);

		if (cR.parent == parent)
		{
			return true;
		}
		else
		{
			if (cR.parent != entt::null)
			{
				// old parent need to cacel relationship.

			}
		}

		pR.childrenCount ++;
		cR.parent = parent;

		if (pR.first == entt::null)
		{
			pR.first = child;
		}
		else
		{
			entt::entity iter = pR.first;
			uint32 iterCount = 0;
			while (true)
			{
				iterCount++;

				auto& iR = m_registry.get<RelationshipComponent>(iter);
				if (iR.next == entt::null)
				{
					iR.next = child;
					cR.prev = iter;
					check(iterCount + 1 == pR.childrenCount);
					break;
				}
				else
				{
					iter = iR.next;
				}
			}
		}

		return true;
	}

	void World::onPostConstruct()
	{
		check(m_root == entt::null);
		m_root = createEntity(u16str("root"), entt::null, true, true);
	}

}