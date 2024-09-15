#pragma once

#include <utils/engine.h>
#include <graphics/graphics.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <scene/scene_common.h>
#include <shader/vsm_shared.h>

namespace chord
{
	struct VirtualShadowMapConfig
	{
		CHORD_DEFAULT_COMPARE_ARCHIVE(VirtualShadowMapConfig);

		// Physical page dim and virtual tile dim, default is 128x128.
		int32 tileDim = 128;

		// Each vsm is 16384x16384, meaning exist 128x128 tile.
		int32 virtualTileCountDim = 128;

		// Highest precision mip level, 2^6 = 64
		int32 firstMipLevel = 6;

		// Lowest precision mip level, 2^22 = 4194304
		int32 lastMipLevel = 22;
	};


	struct ShadowConfig
	{
		CHORD_DEFAULT_COMPARE_ARCHIVE(ShadowConfig);

		VirtualShadowMapConfig vsmConfig;
	};

	class ShadowManager : NonCopyable, public ISceneSystem
	{
	public:
		ARCHIVE_DECLARE;

		explicit ShadowManager();
		virtual ~ShadowManager() = default;

	protected:
		virtual void onDrawUI(SceneRef scene) override;

	private:
		ShadowConfig m_shadowConfig;
	};
}