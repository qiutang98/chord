#include <utils/utils.h>


void chord::debugBreak()
{
#if CHORD_DEBUG
	__debugbreak();
#endif 
}