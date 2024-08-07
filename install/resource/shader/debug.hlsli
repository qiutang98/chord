#ifndef DEBUG_HLSLI
#define DEBUG_HLSLI

#ifdef CHORD_DEBUG
    #define check(x) do { if(!(x)) { printf("check failed! Error line %d, file(%s)\n", __LINE__, __FILE__ ); } } while(0);
#else
    #define check(x)
#endif

#endif // !DEBUG_HLSLI