#include <gctypes.h>

#include "dol.h"
#include "cache.h"
#include "usbgecko.h"

#include "cubeboot_gz.h"
#include "tinf/tinf.h"

#include <ogc/machine/processor.h>

// #define DEBUG

#ifdef DEBUG
#define gprintf usb_OSReport
#else
#define gprintf(...)
#endif

extern const void _end;

void _memcpy(void* dest, const void* src, int count) {
	char* tmp = (char*)dest,* s = (char*)src;
	while (count--)
		*tmp++ = *s++;
}

extern  void _memset(void* s, int c, int count);

// void _memset(void* s, int c, int count) {
// 	char* xs = (char*)s;
// 	while (count--)
// 		*xs++ = c;
// }

extern u8 systemcallhandler_start[], systemcallhandler_end[];

void _main() {
	//make sure "sc" handler is installed
	void *syscallMem = (void*)0x80000C00;
	int syscallLen = (systemcallhandler_end-systemcallhandler_start);
	_memcpy(syscallMem,systemcallhandler_start,syscallLen);
	DCFlushRangeNoSync(syscallMem,syscallLen);
	ICInvalidateRange(syscallMem,syscallLen);


    gprintf("dolloader\n");


    u32 dlen = bswap32(*(u32*)(&cubeboot_gz[cubeboot_gz_size - 4]));
    gprintf("output size = %d\n", dlen);

    u8 *dol_buf = (void*)&_end;
    gprintf("alloc on arena = %08x\n", (u32)dol_buf);

    u32 outlen = (0x817C0000 - (u32)dol_buf);

    gprintf("aaa\n");
    int res = tinf_gzip_uncompress(dol_buf, &outlen, cubeboot_gz, cubeboot_gz_size);
    gprintf("bbb\n");
	if ((res != TINF_OK) || (outlen != dlen)) {
		gprintf("decompression failed (%d)\n", res);
        // u32 *buf = (u32*)dol_buf;
        // gprintf("out = %08x %08x\n", buf[0], buf[1]);
		ppchalt();
	}

	gprintf("decompressed %u bytes\n", outlen);

	DOLHEADER *hdr = (DOLHEADER *)dol_buf;

	u32 max = 0x80003100;

	// Inspect text sections to see if what we found lies in here
	for (int i = 0; i < MAXTEXTSECTION; i++) {
		if (hdr->textAddress[i] && hdr->textLength[i]) {
			_memcpy((void*)hdr->textAddress[i], ((unsigned char*)dol_buf) + hdr->textOffset[i], hdr->textLength[i]);
			u32 _max = hdr->textAddress[i] + hdr->textLength[i];
			if (_max > max) max = _max;
		}
	}

	gprintf("ccc\n");

	// Inspect data sections (shouldn't really need to unless someone was sneaky..)
	for (int i = 0; i < MAXDATASECTION; i++) {
		if (hdr->dataAddress[i] && hdr->dataLength[i]) {
			_memcpy((void*)hdr->dataAddress[i], ((unsigned char*)dol_buf) + hdr->dataOffset[i], hdr->dataLength[i]);
			// u32 _max = hdr->dataAddress[i] + hdr->dataLength[i];
			// if (_max > max) max = _max;
		}
	}
	
	gprintf("ddd %08x\n", max);

	// Clear BSS
	_memset((void*)hdr->bssAddress, 0, hdr->bssLength);

	gprintf("eee\n");

	void (*entrypoint)();
	entrypoint = (void(*)())hdr->entryPoint;

	DCFlushRangeNoSync((void*)hdr->entryPoint, max);
	ICInvalidateRange((void*)hdr->entryPoint, max);
	gprintf("boot\n");
	entrypoint();
	__builtin_unreachable();
}
