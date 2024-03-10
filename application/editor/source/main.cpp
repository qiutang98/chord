#include "viewer.h"

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
		test.set<svUseWave>(false);
		test.set<svTileDim>(3);
		test.set<svLightMode>(ETestMode::fag);
		test.set<svWaveSize>(8);

		ShaderCompileEnvironment env;
		test.modifyCompileEnvironment(env);

		auto a = test.kCount;
		auto b = test.toId();

		FPermutation c;
		c = FPermutation::fromId(b);

		check(c == test);

		CVarSystem::get().getCVarCheck<std::string>("r.log.file.name")->set(kAppName);

		{
			Application::InitConfig config{ };
			config.appName  = kAppName;
			config.iconPath = "resource/icon.png";
			config.bResizable = true;

			// Engine config.
			config.engineConfig.bDebugUtils = true;
			config.engineConfig.bValidation = true;
			config.engineConfig.bRaytracing = true;
			config.engineConfig.bHDR        = false;

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