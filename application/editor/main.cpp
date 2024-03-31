#include "viewer.h"
#include <shader/shader.h>
#include <ui/ui.h>
namespace viewer
{
	using namespace chord;
	using namespace chord::graphics;

	class ImGuiDrawVS : public GlobalShader
	{
	public:
		DECLARE_SUPER_TYPE(GlobalShader);

		static void modifyCompileEnvironment(ShaderCompileEnvironment& o, int32 PermutationId) 
		{ 
		
		}

		static bool shouldCompilePermutation(int32 PermutationId) 
		{
			return true; 
		}
	};

	class ImGuiDrawPS : public GlobalShader
	{
	public:
		DECLARE_SUPER_TYPE(GlobalShader);

		static bool shouldCompilePermutation(int32 PermutationId)
		{
			return true;
		}

		static void modifyCompileEnvironment(ShaderCompileEnvironment& o, int32 PermutationId)
		{

		}
	};

	IMPLEMENT_GLOBAL_SHADER(ImGuiDrawVS, "resource/shader/imgui.hlsl", "mainVS", EShaderStage::Vertex);
	IMPLEMENT_GLOBAL_SHADER(ImGuiDrawPS, "resource/shader/imgui.hlsl", "mainPS", EShaderStage::Pixel);

	void init()
	{
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