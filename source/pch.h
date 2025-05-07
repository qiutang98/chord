#pragma once

#pragma warning(disable : 4005)
#pragma warning(disable : 4996)

#include <volk/volk.h>
#include <GLFW/glfw3.h>
#include <nameof/nameof.hpp>

#define VMA_VULKAN_VERSION 1003000
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vma/vk_mem_alloc.h>

#include <random>
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
#include <queue>
#include <stack>
#include <future>
#include <condition_variable>
#include <type_traits>
#include <algorithm>
#include <chrono>
#include <codecvt>
#include <locale>
#include <cstdint>
#include <type_traits>
#include <atomic>
#include <iostream>
#include <fstream>
#include <execution>
#include <regex>

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
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/integer.hpp>

// namespace alias to ensure all glm header under this file's macro control.
namespace chord
{
	namespace math = glm;
}

// LZ4 as generic cpu compression.
#include <lz4.h>

// RTTR library.
#include <rttr/registration.h>
#include <rttr/registration_friend.h>
#include <rttr/type.h>

// Json library.
#include <nlohmann/json.hpp>

// Ini cpp library.
#include <inipp/inipp.h>

// Cereal archive library.
#include <cereal/access.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/cereal.hpp> 
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/unordered_set.hpp>
#include <cereal/types/list.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/xml.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/map.hpp>

namespace glm
{
	template<class Archive> void serialize(Archive& archive, glm::vec2& v)  { archive(v.x, v.y); }
	template<class Archive> void serialize(Archive& archive, glm::vec3& v)  { archive(v.x, v.y, v.z); }
	template<class Archive> void serialize(Archive& archive, glm::vec4& v)  { archive(v.x, v.y, v.z, v.w); }
	template<class Archive> void serialize(Archive& archive, glm::ivec2& v) { archive(v.x, v.y); }
	template<class Archive> void serialize(Archive& archive, glm::ivec3& v) { archive(v.x, v.y, v.z); }
	template<class Archive> void serialize(Archive& archive, glm::ivec4& v) { archive(v.x, v.y, v.z, v.w); }
	template<class Archive> void serialize(Archive& archive, glm::uvec2& v) { archive(v.x, v.y); }
	template<class Archive> void serialize(Archive& archive, glm::uvec3& v) { archive(v.x, v.y, v.z); }
	template<class Archive> void serialize(Archive& archive, glm::uvec4& v) { archive(v.x, v.y, v.z, v.w); }
	template<class Archive> void serialize(Archive& archive, glm::dvec2& v) { archive(v.x, v.y); }
	template<class Archive> void serialize(Archive& archive, glm::dvec3& v) { archive(v.x, v.y, v.z); }
	template<class Archive> void serialize(Archive& archive, glm::dvec4& v) { archive(v.x, v.y, v.z, v.w); }
	template<class Archive> void serialize(Archive& archive, glm::mat2& m)  { archive(m[0], m[1]); }
	template<class Archive> void serialize(Archive& archive, glm::dmat2& m) { archive(m[0], m[1]); }
	template<class Archive> void serialize(Archive& archive, glm::mat3& m)  { archive(m[0], m[1], m[2]); }
	template<class Archive> void serialize(Archive& archive, glm::mat4& m)  { archive(m[0], m[1], m[2], m[3]); }
	template<class Archive> void serialize(Archive& archive, glm::dmat4& m) { archive(m[0], m[1], m[2], m[3]); }
	template<class Archive> void serialize(Archive& archive, glm::quat& q)  { archive(q.x, q.y, q.z, q.w); }
	template<class Archive> void serialize(Archive& archive, glm::dquat& q) { archive(q.x, q.y, q.z, q.w); }
}

#include <utfcpp/utf8/cpp17.h>
#include <utfcpp/utf8.h>

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <stb/stb_image_resize.h>
#include <stb/stb_dxt.h>

#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_INCLUDE_JSON 
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#include <tinygltf/tiny_gltf.h>

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#include <tinyexr/tinyexr.h>