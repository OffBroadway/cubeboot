#include <gctypes.h>

typedef struct {
    char *name;
    char *reloc_prefix;
    char *patch_suffix;
    u32 crc;
    // u32 sda;
} bios_item;

extern bios_item *current_bios;

void load_ipl();
