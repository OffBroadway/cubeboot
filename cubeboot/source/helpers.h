#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PPC_NOP 0x60000000
#define PPC_BLR 0x4e800020

// credit to https://stackoverflow.com/a/744822/652487 
inline int ensdwith(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}
