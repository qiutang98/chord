#include <scene/scene_manager.h>
#include <asset/asset.h>
#include <project.h>
#include <scene/component/gltf_mesh.h>
#include <scene/component/transform.h>
#include <scene/component/gltf_material.h>
#include <scene/component/sky.h>

namespace chord
{
	SceneManager::SceneManager()
		: ISubsystem("SceneManager") 
	{ 
		auto registerAsset = [this](const UIComponentDrawDetails& type, const std::string& name)
		{
			check(m_registeredComponentUIDrawDetails[name] == nullptr);
			m_registeredComponentUIDrawDetails[name] = &type;
		};

		registerAsset(Transform::kComponentUIDrawDetails, typeid(Transform).name());
		registerAsset(GLTFMeshComponent::kComponentUIDrawDetails, typeid(GLTFMeshComponent).name());
		registerAsset(GLTFMaterialComponent::kComponentUIDrawDetails, typeid(GLTFMaterialComponent).name());
		registerAsset(SkyComponent::kComponentUIDrawDetails, typeid(SkyComponent).name());
	}

	bool SceneManager::onInit()
	{
		return true;
	}

	bool SceneManager::onTick(const SubsystemTickData& tickData)
	{
		if (Project::get().isSetup())
		{
			getActiveScene()->tick(tickData.appTickDaata);
		}

		return true;
	}

	void SceneManager::beforeRelease()
	{

	}

	void SceneManager::onRelease()
	{
		releaseScene();
	}

	SceneRef SceneManager::getActiveScene()
	{
		if (!m_scene.lock())
		{
			static const u16str kDefaultName = u16str("untitled" + Scene::kAssetTypeMeta.suffix);

			// Build a default scene.
			AssetSaveInfo saveInfo = AssetSaveInfo::buildTemp(kDefaultName);
			m_scene = Application::get().getAssetManager().createAsset<Scene>(saveInfo, true);

			// Active scene switch now.
			onSceneLoad.broadcast(m_scene.lock());
		}

		return m_scene.lock();
	}

	void SceneManager::releaseScene()
	{
		if (auto scene = m_scene.lock())
		{
			// Active scene switch now.
			onSceneUnload.broadcast(scene);

			// Scene will explicit unload from asset manager when release.
			Application::get().getAssetManager().unload(scene, true);

			// Clear cache weak ptr.
			m_scene = { };
		}
	}

	bool SceneManager::loadScene(const std::filesystem::path& loadPath)
	{
		if (m_scene.lock())
		{
			releaseScene();
		}

		if (auto newScene = Application::get().getAssetManager().getOrLoadAsset<Scene>(loadPath, true))
		{
			m_scene = newScene;

			newScene->postLoad();
			onSceneLoad.broadcast(newScene);

			return true;
		}

		return false;
	}
}