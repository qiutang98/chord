#include <utils/utils.h>
#include <utils/log.h>

#ifdef WIN32
	#include <consoleapi2.h>
	#include <consoleapi3.h>
#endif

#define UUID_SYSTEM_GENERATOR
#include <stduuid/uuid.h>

chord::UUID chord::generateUUID()
{
	return uuids::to_string(uuids::uuid_system_generator{}());
}

std::filesystem::path chord::buildStillNonExistPath(const std::filesystem::path& p)
{
	const std::u16string rawPath = p.u16string();
	std::u16string pUnique = rawPath;
	auto num = 1;

	while (std::filesystem::exists(pUnique))
	{
		pUnique = rawPath + utf8::utf8to16(std::format("_{}", num));
		num++;
	}

	return pUnique;
}

void chord::setConsoleTitle(const std::string& title)
{
	std::cout << "\033]0;" << title.c_str() << "\007";
}

void chord::setConsoleUtf8()
{
#if _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif 
}

void chord::setConsoleFont(const std::vector<ConsoleFontConfig>& fontTypes)
{
#if _WIN32
	::CONSOLE_FONT_INFOEX cfi { };
	cfi.cbSize = sizeof(cfi);
	cfi.nFont  = 0;
	cfi.FontFamily = FF_DONTCARE;
	cfi.FontWeight = FW_NORMAL;

	// Loop and use first valid font as console font.
	bool bSuccess = false;
	for (const auto& type : fontTypes)
	{
		cfi.dwFontSize.X = type.width;
		cfi.dwFontSize.Y = type.height;
		std::wcscpy(cfi.FaceName, type.name.c_str());
		if (::SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &cfi))
		{
			bSuccess = true;
			break;
		}
	}

	if (!bSuccess)
	{
		LOG_WARN("Can't found required font to set, default font used in console.");
	}
#endif 
}

void chord::debugBreak()
{
#if CHORD_DEBUG
	__debugbreak();
#endif 
}

void chord::namedThread(std::thread& t, const std::wstring& name)
{
#if _WIN32
	SetThreadDescription(t.native_handle(), name.c_str());
#endif
}

void chord::namedCurrentThread(const std::wstring& name)
{
#if _WIN32
	SetThreadDescription(GetCurrentThread(), name.c_str());
#endif
}

bool chord::loadFile(const std::filesystem::path& path, std::vector<char>& binData, const char* mode)
{
	if (auto* fPtr = fopen(path.string().c_str(), mode))
	{
		fseek(fPtr, 0, SEEK_END);
		auto fileSize = ftell(fPtr);
		fseek(fPtr, 0, SEEK_SET);

		binData.resize(fileSize);
		fread(binData.data(), 1, fileSize, fPtr);

		fclose(fPtr);

		return true;
	}
	return false;
}

bool chord::storeFile(const std::filesystem::path& path, const uint8* ptr, uint32 size, const char* mode)
{
	if (auto* fPtr = fopen(path.string().c_str(), mode))
	{
		fwrite(ptr, sizeof(uint8), size, fPtr);
		fclose(fPtr);

		return true;
	}
	return false;
}

chord::DeleteQueue::~DeleteQueue()
{
	clear();
}

void chord::DeleteQueue::push(ResourceRef inT)
{
	m_pendingQueue.push_back(inT);
}

void chord::DeleteQueue::clear()
{
	// From back to end release.
	for (int32 i = int32(m_pendingQueue.size()) - 1; i >= 0; i --)
	{
		m_pendingQueue[i] = nullptr;
	}
	m_pendingQueue.clear();
}

bool chord::decomposeTransform(const math::mat4& transform, math::vec3& translation, math::vec3& rotation, math::vec3& scale)
{
	using namespace math;
	using T = float;

	mat4 LocalMatrix(transform);

	// Normalize the matrix.
	if (epsilonEqual(LocalMatrix[3][3], static_cast<float>(0), epsilon<T>()))
	{
		return false;
	}

	// First, isolate perspective.  This is the messiest.
	if (
		epsilonNotEqual(LocalMatrix[0][3], static_cast<T>(0), epsilon<T>()) ||
		epsilonNotEqual(LocalMatrix[1][3], static_cast<T>(0), epsilon<T>()) ||
		epsilonNotEqual(LocalMatrix[2][3], static_cast<T>(0), epsilon<T>()))
	{
		// Clear the perspective partition
		LocalMatrix[0][3] = LocalMatrix[1][3] = LocalMatrix[2][3] = static_cast<T>(0);
		LocalMatrix[3][3] = static_cast<T>(1);
	}

	// Next take care of translation (easy).
	translation = vec3(LocalMatrix[3]);
	LocalMatrix[3] = vec4(0, 0, 0, LocalMatrix[3].w);

	vec3 Row[3]{};//, Pdum3;

	// Now get scale and shear.
	for (length_t i = 0; i < 3; ++i)
	{
		for (length_t j = 0; j < 3; ++j)
		{
			Row[i][j] = LocalMatrix[i][j];
		}
	}

	// Compute X scale factor and normalize first row.
	scale.x = length(Row[0]);
	Row[0] = detail::scale(Row[0], static_cast<T>(1));
	scale.y = length(Row[1]);
	Row[1] = detail::scale(Row[1], static_cast<T>(1));
	scale.z = length(Row[2]);
	Row[2] = detail::scale(Row[2], static_cast<T>(1));

	// At this point, the matrix (in rows[]) is orthonormal.
	// Check for a coordinate system flip.  If the determinant
	// is -1, then negate the matrix and the scaling factors.
#if 0
	Pdum3 = cross(Row[1], Row[2]); // v3Cross(row[1], row[2], Pdum3);
	if (dot(Row[0], Pdum3) < 0)
	{
		for (length_t i = 0; i < 3; i++)
		{
			scale[i] *= static_cast<T>(-1);
			Row[i] *= static_cast<T>(-1);
		}
	}
#endif

	rotation.y = asin(-Row[0][2]);
	if (cos(rotation.y) != 0.f)
	{
		rotation.x = atan2(Row[1][2], Row[2][2]);
		rotation.z = atan2(Row[0][1], Row[0][0]);
	}
	else
	{
		rotation.x = atan2(-Row[2][0], Row[1][1]);
		rotation.z = 0;
	}

	return true;
}