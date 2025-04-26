#pragma once

#include <scene/scene.h>
#include <scene/scene_node.h>
#include <utils/engine.h>

namespace chord
{
	class SceneManager final : public ISubsystem
	{
	public:
		explicit SceneManager();
		virtual ~SceneManager() = default;

		virtual bool onInit() override;
		virtual bool onTick(const SubsystemTickData& tickData) override;
		virtual void beforeRelease() override;
		virtual void onRelease() override;

		// Get current active scene.
		SceneRef getActiveScene();

		// Release active scene.
		void releaseScene();

		// Load scene from path into active scene.
		bool loadScene(const std::filesystem::path& loadPath);

		// Event when active scene change.
		ChordEvent<SceneRef> onSceneLoad;
		ChordEvent<SceneRef> onSceneUnload;

		const auto& getUIComponentDrawDetailsMap() const
		{
			return m_registeredComponentUIDrawDetails;
		}

	private:
		SceneWeak m_scene;

		// Static const registered component infos.
		std::unordered_map<std::string, const UIComponentDrawDetails*> m_registeredComponentUIDrawDetails;
	};
}