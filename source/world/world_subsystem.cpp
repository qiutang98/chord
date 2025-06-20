#include <world/world_subsystem.h>
#include <project.h>
#include <asset/asset.h>
#include <application/application.h>

namespace chord
{
	WorldSubSystem::WorldSubSystem()
		: ISubsystem("WorldManager")
	{

	}

	bool WorldSubSystem::onInit()
	{
		return true;
	}

	bool WorldSubSystem::onTick(const SubsystemTickData& tickData)
	{
		if (Project::get().isSetup())
		{
			getActiveWorld()->tick(tickData.appTickDaata);
		}

		return true;
	}

	void WorldSubSystem::beforeRelease()
	{

	}

	void WorldSubSystem::onRelease()
	{
		releaseActiveWorld();
	}

	WorldRef WorldSubSystem::getActiveWorld()
	{
		if (!m_world.lock())
		{
			static const u16str kDefaultName = u16str("untitled" + ecs::World::kAssetTypeMeta.suffix);

			// Build a default scene.
			AssetSaveInfo saveInfo = AssetSaveInfo::buildTemp(kDefaultName);
			m_world = Application::get().getAssetManager().createAsset<ecs::World>(saveInfo, true);

			// Active scene switch now.
			onWorldLoad.broadcast(m_world.lock());
		}

		return m_world.lock();
	}

	void WorldSubSystem::releaseActiveWorld()
	{
		if (auto world = m_world.lock())
		{
			// Active scene switch now.
			onWorldUnload.broadcast(world);

			// Scene will explicit unload from asset manager when release.
			Application::get().getAssetManager().unload(world, true);

			// Clear cache weak ptr.
			m_world = { };
		}
	}

	bool WorldSubSystem::loadActiveWorld(const std::filesystem::path& loadPath)
	{
		if (m_world.lock())
		{
			releaseActiveWorld();
		}

		if (auto newWorld = Application::get().getAssetManager().getOrLoadAsset<ecs::World>(loadPath, true))
		{
			m_world = newWorld;

			newWorld->postSubsystemLoad();
			onWorldLoad.broadcast(newWorld);

			return true;
		}

		return false;
	}
}