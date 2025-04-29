#include <utils/string_table.h>

namespace chord
{
	StringTable& StringTable::get()
	{
		static StringTable table { };
		return table;
	}
}