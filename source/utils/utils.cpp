#include <utils/utils.h>

#ifdef WIN32
	#include <Windows.h>
#endif

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
	for (const auto& type : fontTypes)
	{
		cfi.dwFontSize.X = type.width;
		cfi.dwFontSize.Y = type.height;
		std::wcscpy(cfi.FaceName, type.name.c_str());
		if (::SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &cfi))
		{
			break;
		}
	}
#endif 
}

void chord::debugBreak()
{
#if CHORD_DEBUG
	__debugbreak();
#endif 
}