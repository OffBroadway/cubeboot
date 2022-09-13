// #define DOLPHIN
#define TESTING

#define VIDEO_ENABLE
#define CONSOLE_ENABLE
// #define PRINT_PATCHES
// #define PRINT_RELOCS

#if defined(CONSOLE_ENABLE) && !defined(VIDEO_ENABLE)
#define VIDEO_ENABLE
#endif
