#include <utils/delegate.h>

namespace chord
{
	EventHandle EventHandle::requireUnique()
	{
		static uint32 handleVal = 0;

		EventHandle result { };
		result.m_id = handleVal;

		handleVal ++;
		if (handleVal == kUnvalidId)
		{
			handleVal = 0;
		}

		return result;
	}
}