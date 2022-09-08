#include "fatfs/diskio.h"
#include "ffshim.h"

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

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    (void) pdrv;

    if (iface == NULL)
        return RES_NOTRDY;

    if (iface->readSectors(sector, count, buff))
        return RES_OK;
    else
        return RES_ERROR;
}
