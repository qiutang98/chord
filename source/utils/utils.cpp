#include <utils/utils.h>
#include <utils/log.h>

#ifdef WIN32
	#include <consoleapi2.h>
	#include <consoleapi3.h>
#endif

const chord::uint32 chord::kAssetVersion = 0;

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