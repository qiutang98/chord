#pragma once

#include <utils/engine.h>
#include <graphics/graphics.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <scene/scene_common.h>
#include <renderer/render_helper.h>

namespace chord
{




	class ShadowManager : NonCopyable, public ISceneSystem
	{
	public:
		ARCHIVE_DECLARE;

		explicit ShadowManager();
		virtual ~ShadowManager() = default;

		const std::vector<CascadeInfo>& update(const ApplicationTickData& tickData, const ICamera& camera, const float3& lightDirection);

		const ShadowConfig& getConfig() const { return m_shadowConfig; }
	protected:
		virtual void onDrawUI(SceneRef scene) override;


	private:
		uint64 m_updateFrame = ~0;
		std::vector<CascadeInfo> m_cacheCascadeInfos;

	private:
		ShadowConfig m_shadowConfig;
	};
}