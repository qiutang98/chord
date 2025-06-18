#pragma once

#define TRACY_ENABLE

/**
	By default, Tracy will begin profiling even before the program enters the main function.
	However, suppose you don't want to perform a full capture of the application lifetime.
	In that case, you may define the TRACY_ON_DEMAND macro, which will enable profiling only
	when there is an established connection with the server.
**/
#define TRACY_ON_DEMAND

/**
	By default, the Tracy client will announce its presence to the local network12.
	If you want to disable this feature, define the TRACY_NO_BROADCAST macro.
*/
#define TRACY_NO_BROADCAST

// MSVC /ZI build can't treat __LINE__ as a constexpr.
#ifndef TracyConstExpr
	#if defined(_MSC_VER) && (defined(_DEBUG) || defined(DEBUG))
		#define TracyConstExpr const
	#endif 
#endif // !TracyConstExpr

#include <tracy/Tracy.hpp>