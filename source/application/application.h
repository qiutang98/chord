#pragma once

#include <application/window.h>
#include <string>
#include <utils/utils.h>
#include <utils/image.h>
#include <utils/delegate.h>

namespace chord
{
	class Application : NonCopyable
	{
	protected:
		bool m_bInit = false;

		// Application looping state.
		bool m_bLooping = true;

		std::string m_name = { };
		std::unique_ptr<ImageLdr2D> m_icon = nullptr;

		std::vector<std::unique_ptr<Window>> m_windows;

	private:
		// Application should be a singleton.
		Application() = default;

	public:
		static Application& get();

		bool isValid() const { return m_bInit; }

		Window& createWindow(const Window::InitConfig& config);

		[[nodiscard]] bool init(const std::string& appName, std::string iconPath = {});
		void loop();
		void release();



		const ImageLdr2D& getIcon()  const { return *m_icon; }
		const std::string& getName() const { return  m_name; }

		Events<> onInit;
		Events<> onRelease;
	};
}