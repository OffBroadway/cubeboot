#include "reloc.h"
#include "attr.h"

#ifdef DEBUG
// This is actually BS2Report on IPL rev 1.2
__attribute_reloc__ void (*OSReport)(const char* text, ...);
#endif

__attribute_reloc__ bios_pad *pad_status;
__attribute_reloc__ u32 *prev_menu_id;
__attribute_reloc__ u32 *cur_menu_id;
