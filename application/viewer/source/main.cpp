#include "viewer.h"

namespace viewer
{
	using namespace chord;

	void init()
	{
		CVarSystem::get().getCVarCheck<std::string>("r.log.file.name")->set(kAppName);

		{
			Application::InitConfig config{ };
			config.appName = kAppName;
			config.iconPath = "resource/icon.png";
			CHECK(Application::get().init(config));
		}

		{
			graphics::Context::InitConfig config{ };
			config.bInstanceExtensionDebugUtils = true;
			config.bInstanceExtensionGLFW = true;
			config.bInstanceLayerValidation = true;
			CHECK(Application::get().registerSubsystem<graphics::Context>(config));
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

		Window::InitConfig config { };
		config.name = viewer::kAppName;
		config.bResizable = true;
		config.windowShowMode = Window::InitConfig::EWindowMode::Free;

		auto& window0 = Application::get().createWindow(config);

		config.name = "ίχ";
		auto& window1 = Application::get().createWindow(config);


		auto handle0 = window0.onBeforeClosed.add([](const Window& win) -> bool
		{
			LOG_INFO("Call once :0");
			return true;
		});

		auto handle1 = window1.onBeforeClosed.add([](const Window& win) -> bool
		{
			LOG_INFO("Call once :1");
			return true;
		});

		Application::get().loop();
	}



	viewer::release();

	return 0;
}