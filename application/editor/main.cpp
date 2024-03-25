#include "viewer.h"
#include <graphics/shader.h>

namespace viewer
{
	using namespace chord;
	using namespace chord::graphics;


	enum class ETestMode
	{
		sd,
		fag,

		MAX
	};

	class svUseWave   : SHADER_VARIANT_BOOL("USE_WAVE");
	class svTileDim   : SHADER_VARIANT_INT("TILE_SIZE_MODE", 4);
	class svLightMode : SHADER_VARIANT_ENUM("LIGHT_MODE", ETestMode);
	class svWaveSize  : SHADER_VARIANT_SPARSE_INT("WAVE_SIZE", 16, 8, 2);

	using FPermutation = TShaderVariantVector<svUseWave, svTileDim, svLightMode, svWaveSize>;

	void init()
	{
		FPermutation test;
		LOG_INFO(test.kCount);

		test.set<svUseWave>(false);
		test.set<svTileDim>(3);
		test.set<svLightMode>(ETestMode::fag);
		test.set<svWaveSize>(8);

		ShaderCompileEnvironment env;
		test.modifyCompileEnvironment(env);

		test.get<svTileDim>();

		auto a = test.kCount;
		auto b = test.toId();

		FPermutation c;
		c = FPermutation::fromId(test.kCount);

		check(c == test);

		CVarSystem::get().getCVarCheck<std::string>("r.log.file.name")->set("editor/editor");

		{
			Application::InitConfig config{ };
			config.appName  = kAppName;
			config.iconPath = "resource/texture/flowerIcon.png";
			config.bResizable = true;

			// Graphics config.
			config.graphicsConfig.bDebugUtils = true;
			config.graphicsConfig.bValidation = true;
			config.graphicsConfig.bRaytracing = true;
			config.graphicsConfig.bHDR        = false;

			check(Application::get().init(config));
		}


	}

	void release()
	{
		Application::get().release();
	}
}


int main(int argc, const char** argv)
{
	viewer::init();

	{
		using namespace chord;
		using namespace viewer;
		Application::get().loop();
	}



	viewer::release();

	return 0;
}