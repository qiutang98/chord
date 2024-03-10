#pragma once

#pragma warning(disable : 4005)
#pragma warning(disable : 4996)

#include <volk/volk.h>
#include <GLFW/glfw3.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <vector>
#include <string>
#include <iostream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <optional>

// GLM math library config.
// 0. glm force compute on radians.
// 1. glm vulkan depth force 0 to 1.
// 2. glm enable experimental.
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

// namespace alias to ensure all glm header under this file's macro control.
namespace chord
{
	namespace math = glm;
}