#ifndef DEBUG_HLSLI
#define DEBUG_HLSLI

#ifndef __cplusplus

#ifdef CHORD_DEBUG
    #define check(x) do { if(!(x)) { printf("check failed at line(%d), file(%u)\n", __LINE__, CHORD_SHADER_FILE_ID); } } while(0);
#else
    #define check(x)
#endif

#endif

#endif // !DEBUG_HLSLI