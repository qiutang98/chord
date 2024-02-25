#include <utils/cvar.h>

namespace chord
{
	CVarSystem& CVarSystem::get()
	{
		static CVarSystem cvars { };
		return cvars;
	}
}