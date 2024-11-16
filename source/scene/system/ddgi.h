#pragma once

#include <utils/engine.h>
#include <graphics/graphics.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <scene/scene_common.h>
#include <renderer/render_helper.h>

namespace chord
{
	class DDGIManager : NonCopyable, public ISceneSystem
	{
	public:
		ARCHIVE_DECLARE;

		explicit DDGIManager();
		virtual ~DDGIManager() = default;

		// 
		const DDGIConfigCPU& getConfig() const { return m_config; }

	protected:
		virtual void onDrawUI(SceneRef scene) override;

	private:
		DDGIConfigCPU m_config;
	};
}