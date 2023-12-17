#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>
#include <ogc/system.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>

#include "pcg_basic.h"

// Special purpose register indices
#define SPR_ECID_U 924
#define SPR_ECID_M 925
#define SPR_ECID_L 926

// Based on https://github.com/dolphin-emu/dolphin/blob/stable/Source/Core/Core/PowerPC/PowerPC.cpp#L96L98
#define DOLPHIN_ECID_U 0x0d96e200
#define DOLPHIN_ECID_M 0x1840c00d
#define DOLPHIN_ECID_L 0x82bb08e8

bool is_dolphin() {
    if (mfspr(SPR_ECID_U) != DOLPHIN_ECID_U) return FALSE;
    if (mfspr(SPR_ECID_M) != DOLPHIN_ECID_M) return FALSE;
    if (mfspr(SPR_ECID_L) != DOLPHIN_ECID_L) return FALSE;
    return TRUE;
}

u32 generate_random_color() {
    u32 seq = gettick() % 0x20;
    pcg32_srandom(__SYS_GetSystemTime(), 0x97e6 + seq);
    return pcg32_boundedrand(0x00FFFFFF) & 0x00FFFFFF;
}

// credit to https://stackoverflow.com/a/744822/652487
int ensdwith(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

size_t arrlen(char **arr) {
    size_t k;
    for(k = 0; arr[k] != NULL; k++);
    return k;
}
