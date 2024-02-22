#include "viewer.h"

namespace viewer
{
	using namespace chord;

	void init()
	{
		CVarSystem::get().getCVarCheck<std::string>("r.log.file.name")->set(kAppName);

		CHECK(Application::get().init(kAppName, "resources/icon.png"));
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


		auto handle0 = window0.onClosed.add([](const Window& win) -> bool
		{
			LOG_INFO("Call once :0");
			return true;
		});

		auto handle1 = window1.onClosed.add([](const Window& win) -> bool
		{
			LOG_INFO("Call once :1");
			return true;
		});

		Application::get().loop();
	}



	viewer::release();

	return 0;
}