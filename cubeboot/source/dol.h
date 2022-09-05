#ifndef __DOL__
#define __DOL__

#include <gctypes.h>
#include "print.h"

#define PROG_ADDR 0x80100000

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

extern DOLHEADER *dolhdr;
extern u32 minaddress;
extern u32 maxaddress;
extern u32 _entrypoint, _dst, _src, _len;

#endif
