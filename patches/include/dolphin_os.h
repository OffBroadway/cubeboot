#include <gctypes.h>
#include "config.h"

// extern void OSReport(const char* text, ...);
#ifdef DEBUG
extern void (*OSReport)(const char* text, ...);
#else
#define OSReport(...)
#endif

extern u32 __OSBusClock;
extern u64 __OSGetSystemTime(void);
