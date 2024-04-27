#pragma once

#include <scene/scene.h>
#include <scene/scene_node.h>
#include <utils/engine.h>

namespace chord
{
	class SceneManager final : public ISubsystem
	{
	public:
		explicit SceneManager() : ISubsystem("SceneManager") { }
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
		Events<SceneManager, SceneRef> onSceneLoad;
		Events<SceneManager, SceneRef> onSceneUnload;

	private:
		SceneWeak m_scene;
	};
}