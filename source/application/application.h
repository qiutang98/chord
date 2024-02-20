#pragma once

#include <application/window.h>
#include <string>
#include <utils/utils.h>

namespace chord
{
	class Application : NonCopyable
	{
	protected:
		bool m_bInit = false;

		std::string m_name = { };
		GLFWimage   m_icon = { };

	private:
		// Application should be a singleton.
		Application() = default;

	public:
		static Application& get();

		[[nodiscard]] bool init(std::string_view appName, std::string_view iconPath);
		[[nodiscard]] bool release();
	};
}