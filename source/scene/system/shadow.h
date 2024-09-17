#pragma once

#include <utils/engine.h>
#include <graphics/graphics.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <scene/scene_common.h>

namespace chord
{
	struct CascadeInfo
	{
		float4x4 viewProjectMatrix;
		float4 frustumPlanes[6];
	};

	struct CascadeShadowMapConfig
	{
		CHORD_DEFAULT_COMPARE_ARCHIVE(CascadeShadowMapConfig);

		int32 cascadeCount         = 8;
		int32 realtimeCascadeCount = 3;
		int32 cascadeDim           = 1024;

		float cascadeStartDistance = 0.0f;
		float cascadeEndDistance   = 1000.0f; 
		float splitLambda          = 0.9f;
	};


	struct ShadowConfig
	{
		CHORD_DEFAULT_COMPARE_ARCHIVE(ShadowConfig);

		CascadeShadowMapConfig cascadeConfig;
	};

	class ShadowManager : NonCopyable, public ISceneSystem
	{
	public:
		ARCHIVE_DECLARE;

		explicit ShadowManager();
		virtual ~ShadowManager() = default;

		void update(const ApplicationTickData& tickData);

	protected:
		virtual void onDrawUI(SceneRef scene) override;

	private:
		ShadowConfig m_shadowConfig;
	};
}