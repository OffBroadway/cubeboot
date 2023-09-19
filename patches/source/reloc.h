#include "structs.h"
#include "config.h"

#ifdef DEBUG
extern void (*OSReport)(const char* text, ...);
#else
#define OSReport(...)
#endif

extern bios_pad *pad_status;
extern u32 *prev_menu_id;
extern u32 *cur_menu_id;
