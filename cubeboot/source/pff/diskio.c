#include "diskio.h"

#include <string.h>
#include <sdcard/gcsd.h>

#include "../sd.h"
#include "../print.h"

static const DISC_INTERFACE *dev = NULL;

DSTATUS disk_initialize() {
    dev = get_current_device();
    return (dev != NULL) ? 0 : STA_NOINIT;
}

DRESULT disk_readp (
	BYTE *buff,		/* Pointer to the read buffer (NULL:Read bytes are forwarded to the stream) */
	DWORD sector,	/* Sector number (LBA) */
	UINT offset,	/* Byte offset to read from (0..511) */
	UINT count		/* Number of bytes to read (ofs + cnt mus be <= 512) */
) {
    // iprintf("disk_readp 0x%08x, %u, %u, %u\n", (u32)buff, sector, offset, count);

    static u8 sec_buf[512];
    if (!dev->readSectors(sector, 1, sec_buf)) {
        return RES_ERROR;
    }

    void *src = &sec_buf[offset];
    memcpy(buff, src, count);

    return RES_OK;
}