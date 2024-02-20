#pragma once

#include <GLFW/glfw3.h>
#include <utils/noncopyable.h>

namespace chord
{
	class WindowData : NonCopyable
	{
	public:
		// GLFW window handle.
		GLFWwindow* window = nullptr;
	};
}