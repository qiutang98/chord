#pragma once

#include <scene/scene.h>
#include <scene/scene_node.h>
#include <utils/engine.h>
#include <utils/camera.h>

namespace chord
{
	class SceneSubsystem final : public ISubsystem
	{
	public:
		explicit SceneSubsystem();
		virtual ~SceneSubsystem() = default;

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

		void registerCameraView(const ICamera* camera)
		{
			if (!m_registerCameraView.contains(camera))
			{
				m_registerCameraView[camera] = std::make_unique<PerframeCollected>();
			}
		}

		void unregisterCameraView(const ICamera* camera)
		{
			m_registerCameraView.erase(camera);
		}

		const PerframeCollected* getPerframeCollected(const ICamera* camera) const
		{
			return m_registerCameraView.at(camera).get();
		}

		// void (const ICamera* camera, PerframeCollected& collector)
		template<typename Lambda>
		void loopRegisterCamera(Lambda&& func)
		{
			for (auto& cameraPair : m_registerCameraView)
			{
				const ICamera* camera = cameraPair.first;
				PerframeCollected& perframe = *cameraPair.second;
				if (camera->isVisible())
				{
					func(cameraPair.first, perframe);
				}
			}
		}

	private:
		SceneWeak m_scene;

		// Static const registered component infos.
		std::unordered_map<std::string, const UIComponentDrawDetails*> m_registeredComponentUIDrawDetails;

		// 
		std::unordered_map<const ICamera*, std::unique_ptr<PerframeCollected>> m_registerCameraView;
	};
}