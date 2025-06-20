#pragma once

#include <utils/utils.h>
#include <entt/entt.hpp>
#include <asset/asset.h>

namespace chord::ecs
{
	struct VisibleTag { };
	struct StaticTag  { };

	struct RelationshipComponent
	{
		uint32 childrenCount = 0;
		entt::entity first = entt::null;  // 
		entt::entity parent = entt::null; //


		entt::entity prev = entt::null;
		entt::entity next = entt::null;
	};

	struct NameComponent
	{
		u16str name = u16str("untitled");
	};


	class World : public IAsset
	{
		REGISTER_BODY_DECLARE(IAsset);
		friend class AssetManager;

	public:
		World() = default;
		explicit World(const AssetSaveInfo& saveInfo);
		virtual ~World() = default;

		// 
		static const AssetTypeMeta kAssetTypeMeta;

		// Subsystem load current world.
		void postSubsystemLoad();

		// 
		void tick(const ApplicationTickData& tickData);

		uint32 getEntityCount() const
		{
			return m_registry.view<entt::entity>().size();
		}

		entt::entity getRoot() const
		{
			return m_root;
		}

		entt::registry& getRegistry()
		{
			return m_registry;
		}

		const entt::registry& getRegistry() const
		{
			return m_registry;
		}

		bool isRoot(entt::entity entity) const
		{
			return entity == m_root;
		}

		void deleteEntity(entt::entity entity)
		{
			m_registry.destroy(entity);
		}

		entt::entity createEntity(const u16str& name, entt::entity parent, bool bStatic = true, bool bVisible = true);

		NameComponent& getNameComponent(entt::entity entity)
		{
			return m_registry.get<NameComponent>(entity);
		}
		const NameComponent& getNameComponent(entt::entity entity) const
		{
			return m_registry.get<NameComponent>(entity);
		}

		// B is A's descendant ?
		bool isDescendant(entt::entity A, entt::entity B) const;

		bool setParentRelationship(entt::entity parent, entt::entity child, bool bCheckDescendant = true);

	protected:
		// ~IAsset virtual function.
		// Call back when call AssetManager::createAsset
		virtual void onPostConstruct() override;
		virtual bool onSave() override;
		virtual void onUnload() override;
		// ~IAsset virtual function.

		static AssetTypeMeta createTypeMeta();

	private:
		entt::entity m_root = entt::null;
		entt::registry m_registry;
	};
}