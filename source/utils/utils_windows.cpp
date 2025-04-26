#include <utils/log.h>

#ifdef WIN32
	#include <windows.h>
	#include <dbghelp.h>
	#include <consoleapi2.h>
#endif

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

bool chord::isDebuggerAttach()
{
#if _WIN32
	BOOL bRemoteDebuggerPresent = FALSE;
	if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &bRemoteDebuggerPresent))
	{
		if (bRemoteDebuggerPresent)
		{
			return true;
		}
	}

	return IsDebuggerPresent();
#else 
	return false;
#endif 
}

bool chord::createDump(bool bFullDump, const std::wstring& dumpFilePath)
{
#if _WIN32
	if (IsDebuggerPresent())
	{
		// return true; // Skip dump if debugger is present.
	}

	HMODULE dbghelpModule = ::LoadLibrary(TEXT("dbghelp.dll"));
	HANDLE hFile =
		::CreateFile(dumpFilePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	auto scopeExit = makeScopeExit([hFile, dbghelpModule]()
	{
		::CloseHandle(hFile);
		::FreeLibrary(dbghelpModule);
	});

	auto castFunc = reinterpret_cast<BOOL(*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION)>(::GetProcAddress(dbghelpModule, "MiniDumpWriteDump"));
	auto minidumpWriter = std::function<BOOL(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION)>(castFunc);
	if (nullptr == hFile || INVALID_HANDLE_VALUE == hFile)
	{
		return false;
	}

	MINIDUMP_TYPE type = bFullDump ? MiniDumpWithFullMemory : MiniDumpNormal;
	return minidumpWriter(::GetCurrentProcess(), ::GetCurrentProcessId(), hFile, type, nullptr, 0, 0);
#else 
	return false;
#endif
}