#ifndef __DOL__
#define __DOL__

#include <gctypes.h>

/*** A standard DOL header ***/
#define DOLHDRLENGTH 256	/*** All DOLS must have a 256 byte header ***/
#define MAXTEXTSECTION 7
#define MAXDATASECTION 11

/*** A handy DOL structure ***/
typedef struct {
    unsigned int textOffset[MAXTEXTSECTION];
    unsigned int dataOffset[MAXDATASECTION];

    unsigned int textAddress[MAXTEXTSECTION];
    unsigned int dataAddress[MAXDATASECTION];

    unsigned int textLength[MAXTEXTSECTION];
    unsigned int dataLength[MAXDATASECTION];

    unsigned int bssAddress;
    unsigned int bssLength;

    unsigned int entryPoint;
    unsigned int unused[MAXTEXTSECTION];
} DOLHEADER;

u32 DOLSize(DOLHEADER *dol);
int DOLtoMRAM(unsigned char *dol);

#endif
