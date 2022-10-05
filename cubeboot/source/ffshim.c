#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "ffshim.h"

#include "print.h"

// TODO: swtich to a single sector overflow buffer then memmove the aligned read
// TODO: SD cards can usually handle unaligned reads but GCLoader can't, should we check ioType == 'GCSD'

#define SECTOR_SIZE 512
#define MAX_UNALIGNED 10
static u8 sector_buf[SECTOR_SIZE * MAX_UNALIGNED];

const DISC_INTERFACE *iface = NULL;

DSTATUS disk_status(BYTE pdrv)
{
    (void) pdrv;

    if (iface == NULL)
        return STA_NOINIT;
    else if (!iface->isInserted())
        return STA_NOINIT;

    return 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    (void) pdrv;

    if (iface == NULL)
        goto noinit;

    if (!iface->startup())
        goto noinit;

    if (!iface->isInserted())
        goto shutdown;

    return 0;

shutdown:
        iface->shutdown();
noinit:
        iface = NULL;
        return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void) pdrv;

    // iprintf("(A=%d) READING SECTORS sect=%x, count=%u, buff=%08x\n", (u32)buff % 0x20 == 0, (u32)sector, count, (u32)buff);

    if (iface == NULL)
        return RES_NOTRDY;

    if ((u32)buff % 0x20 == 0) {
        if (iface->readSectors(sector, count, buff))
            return RES_OK;
        else
            return RES_ERROR;
    } else {
        if (count > MAX_UNALIGNED) return RES_ERROR;

        if (!iface->readSectors(sector, count, sector_buf))
            return RES_ERROR;

        memcpy(buff, sector_buf, count * SECTOR_SIZE);
        return RES_OK;
    }

    return RES_NOTRDY;
}
