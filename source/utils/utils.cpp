#include <utils/utils.h>
#include <utils/log.h>
#include <utils/cvar.h>
#include <application/application.h>

#include <utils/work_stealing_queue.h>

#define UUID_SYSTEM_GENERATOR
#include <stduuid/uuid.h>

namespace chord
{
	static uint32 sCrashOutputFullDump = 1;
	static AutoCVarRef cVarCrashOutputFullDump(
		"r.crash.fulldump",
		sCrashOutputFullDump,
		"Output fulldump when crash or not."
	);

	static u16str sCrashFileOutputFolder = u16str("save/crash");
	static AutoCVarRef cVarCrashFileOutputFolder(
		"r.crash.folder",
		sCrashFileOutputFolder,
		"Save folder path of dump file.",
		EConsoleVarFlags::ReadOnly);
}

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

bool chord::decomposeTransform(const math::dmat4& transform, math::dvec3& translation, math::dvec3& rotation, math::dvec3& scale)
{
	using namespace math;
	using T = double;

	dmat4 LocalMatrix(transform);

	// Normalize the matrix.
	if (epsilonEqual(LocalMatrix[3][3], 0.0, epsilon<T>()))
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
	translation = dvec3(LocalMatrix[3]);
	LocalMatrix[3] = dvec4(0, 0, 0, LocalMatrix[3].w);

	dvec3 Row[3]{};//, Pdum3;

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

chord::uint64 chord::requireUniqueId()
{
	static uint64 id = 0;
	return id ++;
}

chord::math::mat4 chord::infiniteInvertZPerspectiveRH_ZO(float aspect, float fovy, float zNear)
{
	check(abs(aspect - std::numeric_limits<float>::epsilon()) > 0.0f);
	math::mat4 result = math::zero<math::mat4>();

	const float tanHalfFovy = tan(fovy * 0.5f);
	result[0][0] =  1.0f / (aspect * tanHalfFovy);
	result[1][1] =  1.0f / (tanHalfFovy);
	result[2][3] = -1.0f;
	result[3][2] = zNear;

	return result;
}

static chord::uint64 requireResourceId()
{
	static chord::uint64 sId = 0;
	return sId ++;
}

static chord::uint64 sResourceAliveCounter = 0;

chord::IResource::IResource()
	: m_id(requireResourceId())
{
	sResourceAliveCounter ++;

	if (sResourceAliveCounter % 10000 == 0)
	{
		LOG_TRACE("Live resource count already reach {}.", sResourceAliveCounter);
	}
}

chord::IResource::~IResource()
{
	sResourceAliveCounter --;
}

namespace chord
{
	static inline bool writeMiniDump(bool bFullDump, const std::string& outputFileName)
	{
		auto now = std::chrono::system_clock::now();

		const std::filesystem::path saveFolderName = std::filesystem::path(outputFileName).parent_path();
		if (!std::filesystem::exists(saveFolderName))
		{
			std::filesystem::create_directories(saveFolderName);
		}

		const std::filesystem::path saveFileName = std::filesystem::path(outputFileName).filename();
		const auto finalSaveFileName = saveFileName.string() + formatTimestamp(now) + ".dmp";
		const auto finalCrashSavePath = saveFolderName / finalSaveFileName;

		// Create a minidump file.
		return createDump(bFullDump, finalCrashSavePath.wstring().c_str());
	}

	static inline void reportDumpAndBreak(bool bException, bool bFullDump, const std::string& outputFileName)
	{
		if (isDebuggerAttach())
		{
			__debugbreak();
		}
		else
		{
			if (!outputFileName.empty())
			{
				if (writeMiniDump(bFullDump, outputFileName))
				{
					LOG_TRACE("Minidump already write to '{0}'.", outputFileName);
				}
				else
				{
					LOG_ERROR("Try to write minidump to '{0}' but failed!", outputFileName);
				}
			}
		}

		if (bException)
		{
			throw std::logic_error("Application check failed crash! Read detail from log and crash dump file!");
		}
	}

}

void chord::reportCrash()
{
	constexpr bool bThrowException = true;
	std::string name = std::format("{0}/{1}_crash", sCrashFileOutputFolder.str(), Application::get().getName());
	
	reportDumpAndBreak(bThrowException, sCrashOutputFullDump, name);
}

void chord::reportBreakpoint()
{
	constexpr bool bThrowException = false;
	constexpr bool bFullDump = false;

	std::string name = std::format("{0}/{1}_breakpoint", sCrashFileOutputFolder.str(), Application::get().getName());
	reportDumpAndBreak(bThrowException, bFullDump, name);
}
