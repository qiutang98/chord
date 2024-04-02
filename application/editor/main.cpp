#include "viewer.h"
#include <shader/shader.h>
#include <ui/ui.h>
namespace viewer
{
	using namespace chord;
	using namespace chord::graphics;

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