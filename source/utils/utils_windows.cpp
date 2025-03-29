#ifdef WIN32
	#include <consoleapi2.h>
	#include <consoleapi3.h>
#endif

#include <utils/log.h>

void chord::setConsoleUtf8()
{
#if _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif 
}

void chord::setConsoleFont(const std::vector<ConsoleFontConfig>& fontTypes)
{
#if _WIN32
	::CONSOLE_FONT_INFOEX cfi{ };
	cfi.cbSize = sizeof(cfi);
	cfi.nFont = 0;
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