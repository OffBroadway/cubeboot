
// #define TEST_IPL_PATH
// #define FORCE_IPL_LOAD
// #define DOLPHIN_DELAY_ENABLE

// #define USE_FAT_LIBFAT
#define USE_FAT_FATFS
// #define USE_FAT_PFF

#if defined(USE_FAT_LIBFAT)
#undef USE_FAT_FATFS
#undef USE_FAT_PFF
#endif

#if defined(USE_FAT_FATFS)
#undef USE_FAT_LIBFAT
#undef USE_FAT_PFF
#endif

#if defined(USE_FAT_PFF)
#undef USE_FAT_LIBFAT
#undef USE_FAT_FATFS
#endif

#define VIDEO_ENABLE
// #define CONSOLE_ENABLE

// #define GECKO_PRINT_ENABLE
// #define DOLPHIN_PRINT_ENABLE

// #define PRINT_PATCHES
// #define PRINT_RELOCS

#if defined(CONSOLE_ENABLE) && !defined(VIDEO_ENABLE)
#define VIDEO_ENABLE
#endif
