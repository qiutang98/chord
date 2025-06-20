#pragma once

#include <utils/engine.h>
#include <world/world.h>

namespace chord
{
	using WorldRef = std::shared_ptr<ecs::World>;
	
	class WorldSubSystem final : public ISubsystem
	{
	public:
		explicit WorldSubSystem();
		virtual ~WorldSubSystem() = default;

		virtual bool onInit() override;
		virtual bool onTick(const SubsystemTickData& tickData) override;
		virtual void beforeRelease() override;
		virtual void onRelease() override;

		// Get current active world.
		WorldRef getActiveWorld();

		// Release active world.
		void releaseActiveWorld();

		// Load world from path into active world.
		bool loadActiveWorld(const std::filesystem::path& loadPath);

		ChordEvent<WorldRef> onWorldLoad;
		ChordEvent<WorldRef> onWorldUnload;

	private:
		std::weak_ptr<ecs::World> m_world;
	};
}