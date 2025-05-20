#include <utils/string_table.h>

namespace chord
{
	TStringTable<std::string_view> FName::sFNameTable = { };
	TStringTable<std::u16string_view> FNameU16::sFNameTable = { };
}