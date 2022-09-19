/*-------------------------------------------------------------

gcode.h -- GCODE subsystem

Copyright (C) 2004
Michael Wiedenbauer (shagkur)
Dave Murphy (WinterMute)

Additionally following copyrights apply for the patching system:
 * Copyright (C) 2005 The GameCube Linux Team
 * Copyright (C) 2005 Albert Herranz

Thanks alot guys for that incredible patch!!

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
distribution.


-------------------------------------------------------------*/
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ogc/machine/processor.h>
#include <ogc/cache.h>
#include <ogc/lwp.h>

#include <ogc/irq.h>
#include <ogcsys.h>
#include <ogc/system.h>

#include "gcode.h"
#include "lwp_queue.inl"

#include "print.h"

// #define _GCODE_DEBUG

#define GCODE_BRK							(1<<0)
#define GCODE_DE_MSK						(1<<1)
#define GCODE_DE_INT						(1<<2)
#define GCODE_TC_MSK						(1<<3)
#define GCODE_TC_INT						(1<<4)
#define GCODE_BRK_MSK						(1<<5)
#define GCODE_BRK_INT						(1<<6)

#define GCODE_CVR_INT						(1<<2)
#define GCODE_CVR_MSK						(1<<1)
#define GCODE_CVR_STATE					(1<<0)

#define GCODE_DI_MODE						(1<<2)
#define GCODE_DI_DMA						(1<<1)
#define GCODE_DI_START					(1<<0)

#define GCODE_DISKIDSIZE					0x20
#define GCODE_DRVINFSIZE					0x20

#define GCODE_INQUIRY						0x12000000
#define GCODE_FWSETOFFSET					0x32000000
#define GCODE_FWENABLEEXT					0x55000000
#define GCODE_READ						0xA8000000
#define GCODE_READDISKID					0xA8000040
#define GCODE_SEEK						0xAB000000
#define GCODE_GCODE_READ					0xB2000000
#define GCODE_REQUESTERROR				0xE0000000
#define GCODE_AUDIOSTREAM					0xE1000000
#define GCODE_AUDIOSTATUS					0xE2000000
#define GCODE_STOPMOTOR					0xE3000000
#define GCODE_AUDIOCONFIG					0xE4000000
#define GCODE_FWSETSTATUS					0xEE000000
#define GCODE_FWWRITEMEM					0xFE010100
#define GCODE_FWREADMEM					0xFE010000
#define GCODE_FWCTRLMOTOR					0xFE110000
#define GCODE_FWFUNCCALL					0xFE120000

#define GCODE_MODEL04						0x20020402
#define GCODE_MODEL06						0x20010608
#define GCODE_MODEL08						0x20020823
#define GCODE_MODEL08Q					0x20010831

#define GCODE_FWIRQVECTOR					0x00804c

#define GCODE_DRIVERESET					0x00000001
#define GCODE_CHIPPRESENT					0x00000002
#define GCODE_INTEROPER					0x00000004

/* drive status, status */
#define GCODE_STATUS(s)					((u8)((s)>>24))

#define GCODE_STATUS_READY				0x00
#define GCODE_STATUS_COVER_OPENED			0x01
#define GCODE_STATUS_DISK_CHANGE			0x02
#define GCODE_STATUS_NO_DISK				0x03
#define GCODE_STATUS_MOTOR_STOP			0x04
#define GCODE_STATUS_DISK_ID_NOT_READ		0x05

/* drive status, error */
#define GCODE_ERROR(s)					((u32)((s)&0x00ffffff))

#define GCODE_ERROR_NO_ERROR				0x000000
#define GCODE_ERROR_MOTOR_STOPPED			0x020400
#define GCODE_ERROR_DISK_ID_NOT_READ		0x020401
#define GCODE_ERROR_MEDIUM_NOT_PRESENT	0x023a00
#define GCODE_ERROR_SEEK_INCOMPLETE		0x030200
#define GCODE_ERROR_UNRECOVERABLE_READ	0x031100
#define GCODE_ERROR_TRANSFER_PROTOCOL		0x040800
#define GCODE_ERROR_INVALID_COMMAND		0x052000
#define GCODE_ERROR_AUDIOBUFFER_NOTSET	0x052001
#define GCODE_ERROR_BLOCK_OUT_OF_RANGE	0x052100
#define GCODE_ERROR_INVALID_FIELD			0x052400
#define GCODE_ERROR_INVALID_AUDIO_CMD		0x052401
#define GCODE_ERROR_INVALID_CONF_PERIOD	0x052402
#define GCODE_ERROR_END_OF_USER_AREA		0x056300
#define GCODE_ERROR_MEDIUM_CHANGED		0x062800
#define GCODE_ERROR_MEDIUM_CHANGE_REQ		0x0B5A01

#define GCODE_SPINMOTOR_MASK				0x0000ff00

#define cpu_to_le32(x)					(((x>>24)&0x000000ff) | ((x>>8)&0x0000ff00) | ((x<<8)&0x00ff0000) | ((x<<24)&0xff000000))
#define gcode_may_retry(s)				(GCODE_STATUS(s) == GCODE_STATUS_READY || GCODE_STATUS(s) == GCODE_STATUS_DISK_ID_NOT_READ)

#define _SHIFTL(v, s, w)	\
    ((u32) (((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w)	\
    ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))

typedef void (*gcodecallbacklow)(s32);
typedef void (*gcodestatecb)(gcodecmdblk *);

typedef struct _gcodecmdl {
	s32 cmd;
	void *buf;
	u32 len;
	s64 offset;
	gcodecallbacklow cb;
} gcodecmdl;

typedef struct _gcodecmds {
	void *buf;
	u32 len;
	s64 offset;
} gcodecmds;

static u32 __gcode_initflag = 0;
static u32 __gcode_stopnextint = 0;
static vu32 __gcode_resetoccured = 0;
static u32 __gcode_waitcoverclose = 0;
static u32 __gcode_breaking = 0;
static vu32 __gcode_resetrequired = 0;
static u32 __gcode_canceling = 0;
static u32 __gcode_pauseflag = 0;
static u32 __gcode_pausingflag = 0;
static u64 __gcode_lastresetend = 0;
static u32 __gcode_ready = 0;
static u32 __gcode_resumefromhere = 0;
static u32 __gcode_fatalerror = 0;
static u32 __gcode_lasterror = 0;
static u32 __gcode_internalretries = 0;
static u32 __gcode_autofinishing = 0;
static u32 __gcode_autoinvalidation = 1;
static u32 __gcode_cancellasterror = 0;
static u32 __gcode_drivechecked = 0;
static u32 __gcode_drivestate = 0;
static u32 __gcode_extensionsenabled = TRUE;
static u32 __gcode_lastlen;
static u32 __gcode_nextcmdnum;
static u32 __gcode_workaround;
static u32 __gcode_workaroundseek;
static u32 __gcode_lastcmdwasread;
static u32 __gcode_currcmd;
static u32 __gcode_motorcntrl;
static lwpq_t __gcode_wait_queue;
static syswd_t __gcode_timeoutalarm;
static gcodecmdblk __gcode_block$15;
static gcodecmdblk __gcode_dummycmdblk;
static gcodediskid __gcode_tmpid0 ATTRIBUTE_ALIGN(32);
static gcodedrvinfo __gcode_driveinfo ATTRIBUTE_ALIGN(32);
static gcodecallbacklow __gcode_callback = NULL;
static gcodecallbacklow __gcode_resetcovercb = NULL;
static gcodecallbacklow __gcode_finalunlockcb = NULL;
static gcodecallbacklow __gcode_finalreadmemcb = NULL;
static gcodecallbacklow __gcode_finalsudcb = NULL;
static gcodecallbacklow __gcode_finalstatuscb = NULL;
static gcodecallbacklow __gcode_finaladdoncb = NULL;
static gcodecallbacklow __gcode_finalpatchcb = NULL;
static gcodecallbacklow __gcode_finaloffsetcb = NULL;
static gcodecbcallback __gcode_cancelcallback = NULL;
static gcodecbcallback __gcode_mountusrcb = NULL;
static gcodestatecb __gcode_laststate = NULL;
static gcodecmdblk *__gcode_executing = NULL;
static void *__gcode_usrdata = NULL;
static gcodediskid *__gcode_diskID = (gcodediskid*)0x80000000;

static lwp_queue __gcode_waitingqueue[4];
static gcodecmdl __gcode_cmdlist[4];
static gcodecmds __gcode_cmd_curr,__gcode_cmd_prev;

static u32 __gcodepatchcode_size = 0;
static const u8 *__gcodepatchcode = NULL;

static const u32 __gcode_patchcode04_size = 448;
static const u8 __gcode_patchcode04[] =
{
	0xf7,0x10,0xff,0xf7,0xf4,0x74,0x25,0xd0,0x40,0xf7,0x20,0x4c,0x80,0xf4,0x74,0xd6,
	0x9c,0x08,0xf7,0x20,0xd6,0xfc,0xf4,0x74,0x28,0xae,0x08,0xf7,0x20,0xd2,0xfc,0x80,
	0x0c,0xc4,0xda,0xfc,0xfe,0xc8,0xda,0xfc,0xf5,0x00,0x01,0xe8,0x03,0xfc,0xc1,0x00,
	0xa0,0xf4,0x74,0x09,0xec,0x40,0x10,0xc8,0xda,0xfc,0xf5,0x00,0x02,0xe8,0x03,0xfc,
	0xbc,0x00,0xf4,0x74,0xf9,0xec,0x40,0x80,0x02,0xf0,0x20,0xc8,0x84,0x80,0xc0,0x9c,
	0x81,0xdc,0xb4,0x80,0xf5,0x30,0x00,0xf4,0x44,0xa1,0xd1,0x40,0xf8,0xaa,0x00,0x10,
	0xf4,0xd0,0x9c,0xd1,0x40,0xf0,0x01,0xdc,0xb4,0x80,0xf5,0x30,0x00,0xf7,0x48,0xaa,
	0x00,0xe9,0x07,0xf4,0xc4,0xa1,0xd1,0x40,0x10,0xfe,0xd8,0x32,0xe8,0x1d,0xf7,0x48,
	0xa8,0x00,0xe8,0x28,0xf7,0x48,0xab,0x00,0xe8,0x22,0xf7,0x48,0xe1,0x00,0xe8,0x1c,
	0xf7,0x48,0xee,0x00,0xe8,0x3d,0xd8,0x55,0xe8,0x31,0xfe,0x71,0x04,0xfd,0x22,0x00,
	0xf4,0x51,0xb0,0xd1,0x40,0xa0,0x40,0x04,0x40,0x06,0xea,0x33,0xf2,0xf9,0xf4,0xd2,
	0xb0,0xd1,0x40,0x71,0x04,0xfd,0x0a,0x00,0xf2,0x49,0xfd,0x05,0x00,0x51,0x04,0xf2,
	0x36,0xfe,0xf7,0x21,0xbc,0xff,0xf7,0x31,0xbc,0xff,0xfe,0xf5,0x30,0x01,0xfd,0x7e,
	0x00,0xea,0x0c,0xf5,0x30,0x01,0xc4,0xb0,0x81,0xf5,0x30,0x02,0xc4,0x94,0x81,0xdc,
	0xb4,0x80,0xf8,0xe0,0x00,0x10,0xa0,0xf5,0x10,0x01,0xf5,0x10,0x02,0xf5,0x10,0x03,
	0xfe,0xc8,0xda,0xfc,0xf7,0x00,0xfe,0xff,0xf7,0x31,0xd2,0xfc,0xea,0x0b,0xc8,0xda,
	0xfc,0xf7,0x00,0xfd,0xff,0xf7,0x31,0xd6,0xfc,0xc4,0xda,0xfc,0xcc,0x44,0xfc,0xf7,
	0x00,0xfe,0xff,0xc4,0x44,0xfc,0xf4,0x7d,0x28,0xae,0x08,0xe9,0x07,0xf4,0x75,0x60,
	0xd1,0x40,0xea,0x0c,0xf4,0x7d,0xd6,0x9c,0x08,0xe9,0x05,0xf4,0x75,0x94,0xd1,0x40,
	0xf2,0x7c,0xd0,0x04,0xcc,0x5b,0x80,0xd8,0x01,0xe9,0x02,0x7c,0x04,0x51,0x20,0x71,
	0x34,0xf4,0x7d,0xc1,0x85,0x08,0xe8,0x05,0xfe,0x80,0x01,0xea,0x02,0x80,0x00,0xa5,
	0xd8,0x00,0xe8,0x02,0x85,0x0c,0xc5,0xda,0xfc,0xf4,0x75,0xa0,0xd1,0x40,0x14,0xfe,
	0xf7,0x10,0xff,0xf7,0xf4,0xc9,0xa0,0xd1,0x40,0xd9,0x00,0xe8,0x22,0x21,0xf7,0x49,
	0x08,0x06,0xe9,0x05,0x85,0x02,0xf5,0x10,0x01,0xf4,0x79,0x00,0xf0,0x00,0xe9,0x05,
	0x80,0x00,0xf5,0x10,0x09,0xd9,0x06,0xe9,0x06,0x61,0x06,0xd5,0x06,0x41,0x06,0xf4,
	0xe0,0x9f,0xdc,0xc7,0xf4,0xe0,0xb5,0xcb,0xc7,0x00,0x00,0x00,0x74,0x0a,0x08,0x00,
	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static const u32 __gcode_patchcode06_size = 448;
static const u8 __gcode_patchcode06[] =
{
	0xf7,0x10,0xff,0xf7,0xf4,0x74,0x25,0xd0,0x40,0xf7,0x20,0x4c,0x80,0xf4,0x74,0x42,
	0x9d,0x08,0xf7,0x20,0xd6,0xfc,0xf4,0x74,0x45,0xb1,0x08,0xf7,0x20,0xd2,0xfc,0x80,
	0x0c,0xc4,0xda,0xfc,0xfe,0xc8,0xda,0xfc,0xf5,0x00,0x01,0xe8,0x03,0xfc,0xc1,0x00,
	0xa0,0xf4,0x74,0x09,0xec,0x40,0x10,0xc8,0xda,0xfc,0xf5,0x00,0x02,0xe8,0x03,0xfc,
	0xbc,0x00,0xf4,0x74,0x02,0xed,0x40,0x80,0x02,0xf0,0x20,0xc8,0x78,0x80,0xc0,0x90,
	0x81,0xdc,0xa8,0x80,0xf5,0x30,0x00,0xf4,0x44,0xa1,0xd1,0x40,0xf8,0xaa,0x00,0x10,
	0xf4,0xd0,0x9c,0xd1,0x40,0xf0,0x01,0xdc,0xa8,0x80,0xf5,0x30,0x00,0xf7,0x48,0xaa,
	0x00,0xe9,0x07,0xf4,0xc4,0xa1,0xd1,0x40,0x10,0xfe,0xd8,0x32,0xe8,0x1d,0xf7,0x48,
	0xa8,0x00,0xe8,0x28,0xf7,0x48,0xab,0x00,0xe8,0x22,0xf7,0x48,0xe1,0x00,0xe8,0x1c,
	0xf7,0x48,0xee,0x00,0xe8,0x3d,0xd8,0x55,0xe8,0x31,0xfe,0x71,0x04,0xfd,0x22,0x00,
	0xf4,0x51,0xb0,0xd1,0x40,0xa0,0x40,0x04,0x40,0x06,0xea,0x33,0xf2,0xf9,0xf4,0xd2,
	0xb0,0xd1,0x40,0x71,0x04,0xfd,0x0a,0x00,0xf2,0x49,0xfd,0x05,0x00,0x51,0x04,0xf2,
	0x36,0xfe,0xf7,0x21,0xbc,0xff,0xf7,0x31,0xbc,0xff,0xfe,0xf5,0x30,0x01,0xfd,0x7e,
	0x00,0xea,0x0c,0xf5,0x30,0x01,0xc4,0xa4,0x81,0xf5,0x30,0x02,0xc4,0x88,0x81,0xdc,
	0xa8,0x80,0xf8,0xe0,0x00,0x10,0xa0,0xf5,0x10,0x01,0xf5,0x10,0x02,0xf5,0x10,0x03,
	0xfe,0xc8,0xda,0xfc,0xf7,0x00,0xfe,0xff,0xf7,0x31,0xd2,0xfc,0xea,0x0b,0xc8,0xda,
	0xfc,0xf7,0x00,0xfd,0xff,0xf7,0x31,0xd6,0xfc,0xc4,0xda,0xfc,0xcc,0x44,0xfc,0xf7,
	0x00,0xfe,0xff,0xc4,0x44,0xfc,0xf4,0x7d,0x45,0xb1,0x08,0xe9,0x07,0xf4,0x75,0x60,
	0xd1,0x40,0xea,0x0c,0xf4,0x7d,0x42,0x9d,0x08,0xe9,0x05,0xf4,0x75,0x94,0xd1,0x40,
	0xf2,0x7c,0xd0,0x04,0xcc,0x5b,0x80,0xd8,0x01,0xe9,0x02,0x7c,0x04,0x51,0x20,0x71,
	0x34,0xf4,0x7d,0xb9,0x85,0x08,0xe8,0x05,0xfe,0x80,0x01,0xea,0x02,0x80,0x00,0xa5,
	0xd8,0x00,0xe8,0x02,0x85,0x0c,0xc5,0xda,0xfc,0xf4,0x75,0xa0,0xd1,0x40,0x14,0xfe,
	0xf7,0x10,0xff,0xf7,0xf4,0xc9,0xa0,0xd1,0x40,0xd9,0x00,0xe8,0x22,0x21,0xf7,0x49,
	0x08,0x06,0xe9,0x05,0x85,0x02,0xf5,0x10,0x01,0xf4,0x79,0x00,0xf0,0x00,0xe9,0x05,
	0x80,0x00,0xf5,0x10,0x09,0xd9,0x06,0xe9,0x06,0x61,0x06,0xd5,0x06,0x41,0x06,0xf4,
	0xe0,0xbc,0xdf,0xc7,0xf4,0xe0,0x37,0xcc,0xc7,0x00,0x00,0x00,0x74,0x0a,0x08,0x00,
	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static const u32 __gcode_patchcode08_size = 448;
static const u8 __gcode_patchcode08[] =
{
	0xf7,0x10,0xff,0xf7,0xf4,0x74,0x25,0xd0,0x40,0xf7,0x20,0x4c,0x80,0xf4,0x74,0x32,
	0x9d,0x08,0xf7,0x20,0xd6,0xfc,0xf4,0x74,0x75,0xae,0x08,0xf7,0x20,0xd2,0xfc,0x80,
	0x0c,0xc4,0xda,0xfc,0xfe,0xc8,0xda,0xfc,0xf5,0x00,0x01,0xe8,0x03,0xfc,0xc1,0x00,
	0xa0,0xf4,0x74,0x09,0xec,0x40,0x10,0xc8,0xda,0xfc,0xf5,0x00,0x02,0xe8,0x03,0xfc,
	0xbc,0x00,0xf4,0x74,0xf5,0xec,0x40,0x80,0x02,0xf0,0x20,0xc8,0x80,0x80,0xc0,0x98,
	0x81,0xdc,0xb0,0x80,0xf5,0x30,0x00,0xf4,0x44,0xa1,0xd1,0x40,0xf8,0xaa,0x00,0x10,
	0xf4,0xd0,0x9c,0xd1,0x40,0xf0,0x01,0xdc,0xb0,0x80,0xf5,0x30,0x00,0xf7,0x48,0xaa,
	0x00,0xe9,0x07,0xf4,0xc4,0xa1,0xd1,0x40,0x10,0xfe,0xd8,0x32,0xe8,0x1d,0xf7,0x48,
	0xa8,0x00,0xe8,0x28,0xf7,0x48,0xab,0x00,0xe8,0x22,0xf7,0x48,0xe1,0x00,0xe8,0x1c,
	0xf7,0x48,0xee,0x00,0xe8,0x3d,0xd8,0x55,0xe8,0x31,0xfe,0x71,0x04,0xfd,0x22,0x00,
	0xf4,0x51,0xb0,0xd1,0x40,0xa0,0x40,0x04,0x40,0x06,0xea,0x33,0xf2,0xf9,0xf4,0xd2,
	0xb0,0xd1,0x40,0x71,0x04,0xfd,0x0a,0x00,0xf2,0x49,0xfd,0x05,0x00,0x51,0x04,0xf2,
	0x36,0xfe,0xf7,0x21,0xbc,0xff,0xf7,0x31,0xbc,0xff,0xfe,0xf5,0x30,0x01,0xfd,0x7e,
	0x00,0xea,0x0c,0xf5,0x30,0x01,0xc4,0xac,0x81,0xf5,0x30,0x02,0xc4,0x90,0x81,0xdc,
	0xb0,0x80,0xf8,0xe0,0x00,0x10,0xa0,0xf5,0x10,0x01,0xf5,0x10,0x02,0xf5,0x10,0x03,
	0xfe,0xc8,0xda,0xfc,0xf7,0x00,0xfe,0xff,0xf7,0x31,0xd2,0xfc,0xea,0x0b,0xc8,0xda,
	0xfc,0xf7,0x00,0xfd,0xff,0xf7,0x31,0xd6,0xfc,0xc4,0xda,0xfc,0xcc,0x44,0xfc,0xf7,
	0x00,0xfe,0xff,0xc4,0x44,0xfc,0xf4,0x7d,0x75,0xae,0x08,0xe9,0x07,0xf4,0x75,0x60,
	0xd1,0x40,0xea,0x0c,0xf4,0x7d,0x32,0x9d,0x08,0xe9,0x05,0xf4,0x75,0x94,0xd1,0x40,
	0xf2,0x7c,0xd0,0x04,0xcc,0x5b,0x80,0xd8,0x01,0xe9,0x02,0x7c,0x04,0x51,0x20,0x71,
	0x34,0xf4,0x7d,0xc1,0x85,0x08,0xe8,0x05,0xfe,0x80,0x01,0xea,0x02,0x80,0x00,0xa5,
	0xd8,0x00,0xe8,0x02,0x85,0x0c,0xc5,0xda,0xfc,0xf4,0x75,0xa0,0xd1,0x40,0x14,0xfe,
	0xf7,0x10,0xff,0xf7,0xf4,0xc9,0xa0,0xd1,0x40,0xd9,0x00,0xe8,0x22,0x21,0xf7,0x49,
	0x08,0x06,0xe9,0x05,0x85,0x02,0xf5,0x10,0x01,0xf4,0x79,0x00,0xf0,0x00,0xe9,0x05,
	0x80,0x00,0xf5,0x10,0x09,0xd9,0x06,0xe9,0x06,0x61,0x06,0xd5,0x06,0x41,0x06,0xf4,
	0xe0,0xec,0xdc,0xc7,0xf4,0xe0,0x0e,0xcc,0xc7,0x00,0x00,0x00,0x74,0x0a,0x08,0x00,
	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static const u32 __gcode_patchcodeQ08_size = 448;
static const u8 __gcode_patchcodeQ08[] =
{
	0xf7,0x10,0xff,0xf7,0xf4,0x74,0x25,0xd0,0x40,0xf7,0x20,0x4c,0x80,0xf4,0x74,0x39,
	0x9e,0x08,0xf7,0x20,0xd6,0xfc,0xf4,0x74,0x02,0xb3,0x08,0xf7,0x20,0xd2,0xfc,0x80,
	0x0c,0xc4,0xda,0xfc,0xfe,0xc8,0xda,0xfc,0xf5,0x00,0x01,0xe8,0x03,0xfc,0xc1,0x00,
	0xa0,0xf4,0x74,0x09,0xec,0x40,0x10,0xc8,0xda,0xfc,0xf5,0x00,0x02,0xe8,0x03,0xfc,
	0xbc,0x00,0xf4,0x74,0x02,0xed,0x40,0x80,0x02,0xf0,0x20,0xc8,0x78,0x80,0xc0,0x92,
	0x81,0xdc,0xaa,0x80,0xf5,0x30,0x00,0xf4,0x44,0xa1,0xd1,0x40,0xf8,0xaa,0x00,0x10,
	0xf4,0xd0,0x9c,0xd1,0x40,0xf0,0x01,0xdc,0xaa,0x80,0xf5,0x30,0x00,0xf7,0x48,0xaa,
	0x00,0xe9,0x07,0xf4,0xc4,0xa1,0xd1,0x40,0x10,0xfe,0xd8,0x32,0xe8,0x1d,0xf7,0x48,
	0xa8,0x00,0xe8,0x28,0xf7,0x48,0xab,0x00,0xe8,0x22,0xf7,0x48,0xe1,0x00,0xe8,0x1c,
	0xf7,0x48,0xee,0x00,0xe8,0x3d,0xd8,0x55,0xe8,0x31,0xfe,0x71,0x04,0xfd,0x22,0x00,
	0xf4,0x51,0xb0,0xd1,0x40,0xa0,0x40,0x04,0x40,0x06,0xea,0x33,0xf2,0xf9,0xf4,0xd2,
	0xb0,0xd1,0x40,0x71,0x04,0xfd,0x0a,0x00,0xf2,0x49,0xfd,0x05,0x00,0x51,0x04,0xf2,
	0x36,0xfe,0xf7,0x21,0xbc,0xff,0xf7,0x31,0xbc,0xff,0xfe,0xf5,0x30,0x01,0xfd,0x7e,
	0x00,0xea,0x0c,0xf5,0x30,0x01,0xc4,0xa6,0x81,0xf5,0x30,0x02,0xc4,0x8a,0x81,0xdc,
	0xaa,0x80,0xf8,0xe0,0x00,0x10,0xa0,0xf5,0x10,0x01,0xf5,0x10,0x02,0xf5,0x10,0x03,
	0xfe,0xc8,0xda,0xfc,0xf7,0x00,0xfe,0xff,0xf7,0x31,0xd2,0xfc,0xea,0x0b,0xc8,0xda,
	0xfc,0xf7,0x00,0xfd,0xff,0xf7,0x31,0xd6,0xfc,0xc4,0xda,0xfc,0xcc,0x44,0xfc,0xf7,
	0x00,0xfe,0xff,0xc4,0x44,0xfc,0xf4,0x7d,0x02,0xb3,0x08,0xe9,0x07,0xf4,0x75,0x60,
	0xd1,0x40,0xea,0x0c,0xf4,0x7d,0x39,0x9e,0x08,0xe9,0x05,0xf4,0x75,0x94,0xd1,0x40,
	0xf2,0x7c,0xd0,0x04,0xcc,0x5b,0x80,0xd8,0x01,0xe9,0x02,0x7c,0x04,0x51,0x20,0x71,
	0x34,0xf4,0x7d,0x7f,0x86,0x08,0xe8,0x05,0xfe,0x80,0x01,0xea,0x02,0x80,0x00,0xa5,
	0xd8,0x00,0xe8,0x02,0x85,0x0c,0xc5,0xda,0xfc,0xf4,0x75,0xa0,0xd1,0x40,0x14,0xfe,
	0xf7,0x10,0xff,0xf7,0xf4,0xc9,0xa0,0xd1,0x40,0xd9,0x00,0xe8,0x22,0x21,0xf7,0x49,
	0x08,0x06,0xe9,0x05,0x85,0x02,0xf5,0x10,0x01,0xf4,0x79,0x00,0xf0,0x00,0xe9,0x05,
	0x80,0x00,0xf5,0x10,0x09,0xd9,0x06,0xe9,0x06,0x61,0x06,0xd5,0x06,0x41,0x06,0xf4,
	0xe0,0x79,0xe1,0xc7,0xf4,0xe0,0xeb,0xcd,0xc7,0x00,0x00,0x00,0xa4,0x0a,0x08,0x00,
	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static vu32* const _piReg = (u32*)0xCC003000;
static vu32* const _diReg = (u32*)0xCC006000;

static const u8 __gcode_unlockcmd$221[12] = {0xff,0x01,'M','A','T','S','H','I','T','A',0x02,0x00};
static const u8 __gcode_unlockcmd$222[12] = {0xff,0x00,'D','V','D','-','G','A','M','E',0x03,0x00};
static const u32 __gcode_errortable[] = {
	0x00000000, 0x00023a00, 0x00062800, 0x00030200,
	0x00031100, 0x00052000, 0x00052001, 0x00052100,
	0x00052400, 0x00052401, 0x00052402, 0x000B5A01,
	0x00056300, 0x00020401, 0x00020400, 0x00040800,
	0x00100007, 0x00000000
};

static void __gcode_statecheckid(void);
static void __gcode_stategettingerror(void);
static void __gcode_statecoverclosed(void);
static void __gcode_stateready(void);
static void __gcode_statemotorstopped(void);
static void __gcode_statetimeout(void);
static void __gcode_stategotoretry(void);
static void __gcode_stateerror(s32 result);
static void __gcode_statecoverclosed_cmd(gcodecmdblk *block);
static void __gcode_statebusy(gcodecmdblk *block);
static s32 __issuecommand(s32 prio,gcodecmdblk *block);

static void GCODE_LowReset(u32 reset_mode);
static s32 GCODE_LowSeek(s64 offset,gcodecallbacklow cb);
static s32 GCODE_LowRead(void *buf,u32 len,s64 offset,gcodecallbacklow cb);
static s32 GCODE_LowReadDiskID(gcodediskid *diskID,gcodecallbacklow cb);
static s32 GCODE_LowRequestError(gcodecallbacklow cb);
static s32 GCODE_LowStopMotor(gcodecallbacklow cb);
static s32 GCODE_LowInquiry(gcodedrvinfo *info,gcodecallbacklow cb);
static s32 GCODE_LowWaitCoverClose(gcodecallbacklow cb);
static s32 GCODE_LowGetCoverStatus(void);
static s32 GCODE_LowAudioStream(u32 subcmd,u32 len,s64 offset,gcodecallbacklow cb);
static s32 GCODE_LowAudioBufferConfig(s32 enable,u32 size,gcodecallbacklow cb);
static s32 GCODE_LowRequestAudioStatus(u32 subcmd,gcodecallbacklow cb);
static s32 GCODE_LowEnableExtensions(u8 enable,gcodecallbacklow cb);
static s32 GCODE_LowSpinMotor(u32 mode,gcodecallbacklow cb);
static s32 GCODE_LowSetStatus(u32 status,gcodecallbacklow cb);
static s32 GCODE_LowUnlockDrive(gcodecallbacklow cb);
static s32 GCODE_LowPatchDriveCode(gcodecallbacklow cb);
static s32 GCODE_LowSpinUpDrive(gcodecallbacklow cb);
static s32 GCODE_LowControlMotor(u32 mode,gcodecallbacklow cb);
static s32 GCODE_LowFuncCall(u32 address,gcodecallbacklow cb);
static s32 GCODE_LowReadmem(u32 address,void *buffer,gcodecallbacklow cb);
static s32 GCODE_LowSetGCMOffset(s64 offset,gcodecallbacklow cb);
static s32 GCODE_LowSetOffset(s64 offset,gcodecallbacklow cb);
static s32 GCODE_GcodeLowRead(void *buf,u32 len,u32 offset,gcodecallbacklow cb);

extern void udelay(int us);
extern u32 diff_msec(unsigned long long start,unsigned long long end);
extern long long gettime(void);
extern void __MaskIrq(u32);
extern void __UnmaskIrq(u32);
extern syssramex* __SYS_LockSramEx(void);
extern u32 __SYS_UnlockSramEx(u32 write);

static u8 err2num(u32 errorcode)
{
	u32 i;

	i=0;
	while(i<18) {
		if(errorcode==__gcode_errortable[i]) return i;
		i++;
	}
	if(errorcode<0x00100000 || errorcode>0x00100008) return 29;

	return 17;
}

static u8 convert(u32 errorcode)
{
	u8 err,err_num;

	if((errorcode-0x01230000)==0x4567) return 255;
	else if((errorcode-0x01230000)==0x4568) return 254;

	err = _SHIFTR(errorcode,24,8);
	err_num = err2num((errorcode&0x00ffffff));
	if(err>0x06) err = 0x06;

	return err_num+(err*30);
}

static void __gcode_clearwaitingqueue(void)
{
	u32 i;

	for(i=0;i<4;i++)
		__lwp_queue_init_empty(&__gcode_waitingqueue[i]);
}

static s32 __gcode_checkwaitingqueue(void)
{
	u32 i;
	u32 level;

	_CPU_ISR_Disable(level);
	for(i=0;i<4;i++) {
		if(!__lwp_queue_isempty(&__gcode_waitingqueue[i])) break;
	}
	_CPU_ISR_Restore(level);
	return (i<4);
}

static s32 __gcode_pushwaitingqueue(s32 prio,gcodecmdblk *block)
{
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_pushwaitingqueue(%d,%p,%p)\n",prio,block,block->cb);
#endif
	_CPU_ISR_Disable(level);
	__lwp_queue_appendI(&__gcode_waitingqueue[prio],&block->node);
	_CPU_ISR_Restore(level);
	return 1;
}

static gcodecmdblk* __gcode_popwaitingqueueprio(s32 prio)
{
	u32 level;
	gcodecmdblk *ret = NULL;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_popwaitingqueueprio(%d)\n",prio);
#endif
	_CPU_ISR_Disable(level);
	ret = (gcodecmdblk*)__lwp_queue_firstnodeI(&__gcode_waitingqueue[prio]);
	_CPU_ISR_Restore(level);
#ifdef _GCODE_DEBUG
	iprintf("__gcode_popwaitingqueueprio(%p,%p)\n",ret,ret->cb);
#endif
	return ret;
}

static gcodecmdblk* __gcode_popwaitingqueue(void)
{
	u32 i,level;
	gcodecmdblk *ret = NULL;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_popwaitingqueue()\n");
#endif
	_CPU_ISR_Disable(level);
	for(i=0;i<4;i++) {
		if(!__lwp_queue_isempty(&__gcode_waitingqueue[i])) {
			_CPU_ISR_Restore(level);
			ret = __gcode_popwaitingqueueprio(i);
			return ret;
		}
	}
	_CPU_ISR_Restore(level);
	return NULL;
}

static void __gcode_timeouthandler(syswd_t alarm,void *cbarg)
{
	gcodecallbacklow cb;

	__MaskIrq(IRQMASK(IRQ_PI_DI));
	cb = __gcode_callback;
	if(cb) cb(0x10);
}

static void __gcode_storeerror(u32 errorcode)
{
	u8 err;
	syssramex *ptr;

	err = convert(errorcode);
	ptr = __SYS_LockSramEx();
	ptr->dvderr_code = err;
	__SYS_UnlockSramEx(1);
}

static u32 __gcode_categorizeerror(u32 errorcode)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_categorizeerror(%08x)\n",errorcode);
#endif
	if((errorcode-0x20000)==0x0400) {
		__gcode_lasterror = errorcode;
		return 1;
	}

	if(GCODE_ERROR(errorcode)==GCODE_ERROR_MEDIUM_CHANGED
		|| GCODE_ERROR(errorcode)==GCODE_ERROR_MEDIUM_NOT_PRESENT
		|| GCODE_ERROR(errorcode)==GCODE_ERROR_MEDIUM_CHANGE_REQ
		|| (GCODE_ERROR(errorcode)-0x40000)==0x3100) return 0;

	__gcode_internalretries++;
	if(__gcode_internalretries==2) {
		if(__gcode_lasterror==GCODE_ERROR(errorcode)) {
			__gcode_lasterror = GCODE_ERROR(errorcode);
			return 1;
		}
		__gcode_lasterror = GCODE_ERROR(errorcode);
		return 2;
	}

	__gcode_lasterror = GCODE_ERROR(errorcode);
	if(GCODE_ERROR(errorcode)!=GCODE_ERROR_UNRECOVERABLE_READ) {
		if(__gcode_executing->cmd!=0x0005) return 3;
	}
	return 2;
}

static void __SetupTimeoutAlarm(const struct timespec *tp)
{
	SYS_SetAlarm(__gcode_timeoutalarm,tp,__gcode_timeouthandler,NULL);
}

static void __Read(void *buffer,u32 len,s64 offset,gcodecallbacklow cb)
{
	struct timespec tb;
#ifdef _GCODE_DEBUG
	iprintf("__Read(%p,%d,%lld)\n",buffer,len,offset);
#endif
	__gcode_callback = cb;
	__gcode_stopnextint = 0;
	__gcode_lastcmdwasread = 1;

	_diReg[2] = GCODE_READ;
	_diReg[3] = (u32)(offset>>2);
	_diReg[4] = len;
	_diReg[5] = (u32)buffer;
	_diReg[6] = len;

	__gcode_lastlen = len;

	_diReg[7] = (GCODE_DI_DMA|GCODE_DI_START);

	if(len>0x00a00000) {
		tb.tv_sec = 20;
		tb.tv_nsec = 0;
		__SetupTimeoutAlarm(&tb);
	} else {
		tb.tv_sec = 10;
		tb.tv_nsec = 0;
		__SetupTimeoutAlarm(&tb);
	}
}

static void __DoRead(void *buffer,u32 len,s64 offset,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("__DoRead(%p,%d,%lld)\n",buffer,len,offset);
#endif
	__gcode_nextcmdnum = 0;
	__gcode_cmdlist[0].cmd = -1;
	__Read(buffer,len,offset,cb);
}

static u32 __ProcessNextCmd(void)
{
	u32 cmd_num;
#ifdef _GCODE_DEBUG
	iprintf("__ProcessNextCmd(%d)\n",__gcode_nextcmdnum);
#endif
	cmd_num = __gcode_nextcmdnum;
	if(__gcode_cmdlist[cmd_num].cmd==0x0001) {
		__gcode_nextcmdnum++;
		__Read(__gcode_cmdlist[cmd_num].buf,__gcode_cmdlist[cmd_num].len,__gcode_cmdlist[cmd_num].offset,__gcode_cmdlist[cmd_num].cb);
		return 1;
	}

	if(__gcode_cmdlist[cmd_num].cmd==0x0002) {
		__gcode_nextcmdnum++;
		GCODE_LowSeek(__gcode_cmdlist[cmd_num].offset,__gcode_cmdlist[cmd_num].cb);
		return 1;
	}
	return 0;
}

static void __GCODELowWATypeSet(u32 workaround,u32 workaroundseek)
{
	u32 level;

	_CPU_ISR_Disable(level);
	__gcode_workaround = workaround;
	__gcode_workaroundseek = workaroundseek;
	_CPU_ISR_Restore(level);
}

static void __GCODEInitWA(void)
{
	__gcode_nextcmdnum = 0;
	__GCODELowWATypeSet(0,0);
}

static s32 __gcode_checkcancel(u32 cancelpt)
{
	gcodecmdblk *block;

	if(__gcode_canceling) {
		__gcode_resumefromhere = cancelpt;
		__gcode_canceling = 0;

		block = __gcode_executing;
		__gcode_executing = &__gcode_dummycmdblk;

		block->state = GCODE_STATE_CANCELED;
		if(block->cb) block->cb(GCODE_ERROR_CANCELED,block);
		if(__gcode_cancelcallback) __gcode_cancelcallback(GCODE_ERROR_OK,block);

		__gcode_stateready();
		return 1;
	}
	return 0;
}

static void __gcode_stateretrycb(s32 result)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_stateretrycb(%08x)\n",result);
#endif
	if(result==0x0010) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_statetimeout();
		return;
	}

	if(result&0x0002) __gcode_stateerror(0x01234567);
	if(result==0x0001) {
		__gcode_statebusy(__gcode_executing);
		return;
	}
}

static void __gcode_unrecoverederrorretrycb(s32 result)
{
	u32 val;

	if(result==0x0010) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_statetimeout();
		return;
	}

	__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
	if(result&0x0002) __gcode_stateerror(0x01234567);
	else {
		val = _diReg[8];
		__gcode_stateerror(val);
	}
}

static void __gcode_unrecoverederrorcb(s32 result)
{
	if(result==0x0010) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_statetimeout();
		return;
	}
	if(result&0x0001) {
		__gcode_stategotoretry();
		return;
	}
	if(!(result==0x0002)) GCODE_LowRequestError(__gcode_unrecoverederrorretrycb);
}

static void __gcode_stateerrorcb(s32 result)
{
	gcodecmdblk *block;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_stateerrorcb(%d)\n",result);
#endif
	if(result==0x0010) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_statetimeout();
		return;
	}

	__gcode_fatalerror = 1;
	block = __gcode_executing;
	__gcode_executing = &__gcode_dummycmdblk;
	if(block->cb) block->cb(GCODE_ERROR_FATAL,block);
	if(__gcode_canceling) {
		__gcode_canceling = 0;
		if(__gcode_cancelcallback) __gcode_cancelcallback(GCODE_ERROR_OK,block);
	}
	__gcode_stateready();
}

static void __gcode_stategettingerrorcb(s32 result)
{
	s32 ret;
	u32 val,cnclpt;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_stategettingerrorcb(%d)\n",result);
#endif
	if(result==0x0010) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_statetimeout();
		return;
	}
	if(result&0x0002) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_stateerror(0x01234567);
		return;
	}
	if(result==0x0001) {
		val = _diReg[8];
		ret = __gcode_categorizeerror(val);
#ifdef _GCODE_DEBUG
		iprintf("__gcode_stategettingerrorcb(status = %02x, err = %08x, category = %d)\n",GCODE_STATUS(val),GCODE_ERROR(val),ret);
#endif
		if(ret==1) {
			__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
			__gcode_stateerror(val);
			return;
		} else if(ret==2 || ret==3) cnclpt = 0;
		else {
			if(GCODE_STATUS(val)==GCODE_STATUS_COVER_OPENED) cnclpt = 4;
			else if(GCODE_STATUS(val)==GCODE_STATUS_DISK_CHANGE) cnclpt = 6;
			else if(GCODE_STATUS(val)==GCODE_STATUS_NO_DISK) cnclpt = 3;
			else cnclpt = 5;
		}
		if(__gcode_checkcancel(cnclpt)) return;

		if(ret==2) {
			if(gcode_may_retry(val)) {
				// disable the extensions if they're enabled and we were trying to read the disc ID
				if(__gcode_executing->cmd==0x0005) {
					__gcode_lasterror = 0;
					__gcode_extensionsenabled = FALSE;
					GCODE_LowSpinUpDrive(__gcode_stateretrycb);
					return;
				}
				__gcode_statebusy(__gcode_executing);
			} else {
				__gcode_storeerror(val);
				__gcode_stategotoretry();
			}
			return;
		} else if(ret==3) {
			if(GCODE_ERROR(val)==GCODE_ERROR_UNRECOVERABLE_READ) {
				GCODE_LowSeek(__gcode_executing->offset,__gcode_unrecoverederrorcb);
				return;
			} else {
				__gcode_laststate(__gcode_executing);
				return;
			}
		} else if(GCODE_STATUS(val)==GCODE_STATUS_COVER_OPENED) {
			__gcode_executing->state = GCODE_STATE_COVER_OPEN;
			__gcode_statemotorstopped();
			return;
		} else if(GCODE_STATUS(val)==GCODE_STATUS_DISK_CHANGE) {
			__gcode_executing->state = GCODE_STATE_COVER_CLOSED;
			__gcode_statecoverclosed();
			return;
		} else if(GCODE_STATUS(val)==GCODE_STATUS_NO_DISK) {
			__gcode_executing->state = GCODE_STATE_NO_DISK;
			__gcode_statemotorstopped();
			return;
		}
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_stateerror(0x01234567);
		return;
	}
}

static void __gcode_statebusycb(s32 result)
{
	u32 val;
	gcodecmdblk *block;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_statebusycb(%04x)\n",result);
#endif
	if(result==0x0010) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_statetimeout();
		return;
	}
	if(__gcode_currcmd==0x0003 || __gcode_currcmd==0x000f) {
		if(result&0x0002) {
			__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
			__gcode_stateerror(0x01234567);
			return;
		}
		if(result==0x0001) {
			__gcode_internalretries = 0;
			if(__gcode_currcmd==0x000f) __gcode_resetrequired = 1;
			if(__gcode_checkcancel(7)) return;

			__gcode_executing->state = GCODE_STATE_MOTOR_STOPPED;
			__gcode_statemotorstopped();
			return;
		}
	}
	if(result&0x0004) {

	}
	if(__gcode_currcmd==0x0001 || __gcode_currcmd==0x0004
		|| __gcode_currcmd==0x0005 || __gcode_currcmd==0x000e
		|| __gcode_currcmd==0x0014) {
		__gcode_executing->txdsize += (__gcode_executing->currtxsize-_diReg[6]);
	}
	if(result&0x0008) {
		__gcode_canceling = 0;
		block = __gcode_executing;
		__gcode_executing = &__gcode_dummycmdblk;
		__gcode_executing->state = GCODE_STATE_CANCELED;
		if(block->cb) block->cb(GCODE_ERROR_CANCELED,block);
		if(__gcode_cancelcallback) __gcode_cancelcallback(GCODE_ERROR_OK,block);
		__gcode_stateready();
		return;
	}
	if(result&0x0001) {
		__gcode_internalretries = 0;
		if(__gcode_currcmd==0x0010) {
			block = __gcode_executing;
			__gcode_executing = &__gcode_dummycmdblk;
			block->state = GCODE_STATE_END;
			if(block->cb) block->cb(GCODE_ERROR_OK,block);
			__gcode_stateready();
			return;
		}
		if(__gcode_checkcancel(0)) return;

		if(__gcode_currcmd==0x0001 || __gcode_currcmd==0x0004
			|| __gcode_currcmd==0x0005 || __gcode_currcmd==0x000e
			|| __gcode_currcmd==0x0014) {
#ifdef _GCODE_DEBUG
			iprintf("__gcode_statebusycb(%p,%p)\n",__gcode_executing,__gcode_executing->cb);
#endif
			if(__gcode_executing->txdsize!=__gcode_executing->len) {
				__gcode_statebusy(__gcode_executing);
				return;
			}
			block = __gcode_executing;
			__gcode_executing = &__gcode_dummycmdblk;
			block->state = GCODE_STATE_END;
			if(block->cb) block->cb(block->txdsize,block);
			__gcode_stateready();
			return;
		}
		if(__gcode_currcmd==0x0009 || __gcode_currcmd==0x000a
			|| __gcode_currcmd==0x000b || __gcode_currcmd==0x000c) {

			val = _diReg[8];
			if(__gcode_currcmd==0x000a || __gcode_currcmd==0x000b) val <<= 2;

			block = __gcode_executing;
			__gcode_executing = &__gcode_dummycmdblk;
			block->state = GCODE_STATE_END;
			if(block->cb) block->cb(val,block);
			__gcode_stateready();
			return;
		}
		if(__gcode_currcmd==0x0006) {
			if(!__gcode_executing->currtxsize) {
				if(_diReg[8]&0x0001) {
					block = __gcode_executing;
					__gcode_executing = &__gcode_dummycmdblk;
					block->state = GCODE_STATE_IGNORED;
					if(block->cb) block->cb(GCODE_ERROR_IGNORED,block);
					__gcode_stateready();
					return;
				}
				__gcode_autofinishing = 0;
				__gcode_executing->currtxsize = 1;
				GCODE_LowAudioStream(0,__gcode_executing->len,__gcode_executing->offset,__gcode_statebusycb);
				return;
			}

			block = __gcode_executing;
			__gcode_executing = &__gcode_dummycmdblk;
			block->state = GCODE_STATE_END;
			if(block->cb) block->cb(GCODE_ERROR_OK,block);
			__gcode_stateready();
			return;
		}
		if(__gcode_currcmd==0x0011) {
			if(__gcode_drivestate&GCODE_CHIPPRESENT) {
				block = __gcode_executing;
				__gcode_executing = &__gcode_dummycmdblk;
				block->state = GCODE_STATE_END;
				if(block->cb) block->cb(GCODE_DISKIDSIZE,block);
				__gcode_stateready();
				return;
			}
		}

		block = __gcode_executing;
		__gcode_executing = &__gcode_dummycmdblk;
		block->state = GCODE_STATE_END;
		if(block->cb) block->cb(GCODE_ERROR_OK,block);
		__gcode_stateready();
		return;
	}
	if(result==0x0002) {
#ifdef _GCODE_DEBUG
		iprintf("__gcode_statebusycb(%d,%d)\n",__gcode_executing->txdsize,__gcode_executing->len);
#endif
		if(__gcode_currcmd==0x000e) {
			__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
			__gcode_stateerror(0x01234567);
			return;
		}
		if((__gcode_currcmd==0x0001 || __gcode_currcmd==0x0004
			|| __gcode_currcmd==0x0005 || __gcode_currcmd==0x000e
			|| __gcode_currcmd==0x0014)
			&& __gcode_executing->txdsize==__gcode_executing->len) {
				if(__gcode_checkcancel(0)) return;

				block = __gcode_executing;
				__gcode_executing = &__gcode_dummycmdblk;
				block->state = GCODE_STATE_END;
				if(block->cb) block->cb(block->txdsize,block);
				__gcode_stateready();
				return;
		}
	}
	__gcode_stategettingerror();
}

static void __gcode_synccb(s32 result,gcodecmdblk *block)
{
	LWP_ThreadBroadcast(__gcode_wait_queue);
}

static void __gcode_statemotorstoppedcb(s32 result)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_statemotorstoppedcb(%08x)\n",result);
#endif
	_diReg[1] = 0;
	__gcode_executing->state = GCODE_STATE_COVER_CLOSED;
	__gcode_statecoverclosed();
}

static void __gcode_statecoverclosedcb(s32 result)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_statecoverclosedcb(%08x)\n",result);
#endif
	if(result==0x0010) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_statetimeout();
		return;
	}

	if(!(result&0x0006)) {
		__gcode_internalretries = 0;
		__gcode_statecheckid();
		return;
	}

	if(result==0x0002) __gcode_stategettingerror();
}

static void __gcode_statecheckid1cb(s32 result)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_statecheckid1cb(%08x)\n",result);
#endif
	__gcode_ready = 1;
	if(memcmp(__gcode_diskID,&__gcode_tmpid0,GCODE_DISKIDSIZE)) memcpy(__gcode_diskID,&__gcode_tmpid0,GCODE_DISKIDSIZE);
	LWP_ThreadBroadcast(__gcode_wait_queue);
}

static void __gcode_stategotoretrycb(s32 result)
{
	if(result==0x0010) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_statetimeout();
		return;
	}
	if(result&0x0002) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		__gcode_stateerror(0x01234567);
		return;
	}
	if(result==0x0001) {
		__gcode_internalretries = 0;
		if(__gcode_currcmd==0x0004 || __gcode_currcmd==0x0005
			|| __gcode_currcmd==0x000d || __gcode_currcmd==0x000f) {
			__gcode_resetrequired = 1;
			if(__gcode_checkcancel(2)) return;

			__gcode_executing->state = GCODE_STATE_RETRY;
			__gcode_statemotorstopped();
		}
	}
}

static void __gcode_getstatuscb(s32 result)
{
	u32 val,*pn_data;
	gcodecallbacklow cb;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_getstatuscb(%d)\n",result);
#endif
	if(result==0x0010) {
		__gcode_statetimeout();
		return;
	}
	if(result==0x0001) {
		val = _diReg[8];

		pn_data = __gcode_usrdata;
		__gcode_usrdata = NULL;
		if(pn_data) pn_data[0] = val;

		cb = __gcode_finalstatuscb;
		__gcode_finalstatuscb = NULL;
		if(cb) cb(result);
		return;
	}
	__gcode_stategettingerror();
}

static void __gcode_readmemcb(s32 result)
{
	u32 val,*pn_data;
	gcodecallbacklow cb;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_readmemcb(%d)\n",result);
#endif
	if(result==0x0010) {
		__gcode_statetimeout();
		return;
	}
	if(result==0x0001) {
		val = _diReg[8];

		pn_data = __gcode_usrdata;
		__gcode_usrdata = NULL;
		if(pn_data) pn_data[0] = val;

		cb = __gcode_finalreadmemcb;
		__gcode_finalreadmemcb = NULL;
		if(cb) cb(result);
		return;
	}
	__gcode_stategettingerror();
}

static void __gcode_cntrldrivecb(s32 result)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_cntrldrivecb(%d)\n",result);
#endif
	if(result==0x0010) {
		__gcode_statetimeout();
		return;
	}
	if(result==0x0001) {
		GCODE_LowSpinMotor(__gcode_motorcntrl,__gcode_finalsudcb);
		return;
	}
	__gcode_stategettingerror();
}

static void __gcode_setgcmoffsetcb(s32 result)
{
	s64 *pn_data,offset;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_setgcmoffsetcb(%d)\n",result);
#endif
	if(result==0x0010) {
		__gcode_statetimeout();
		return;
	}
	if(result==0x0001) {
		pn_data = (s64*)__gcode_usrdata;
		__gcode_usrdata = NULL;

		offset = 0;
		if(pn_data) offset = *pn_data;
		GCODE_LowSetOffset(offset,__gcode_finaloffsetcb);
		return;
	}
	__gcode_stategettingerror();
}

static void __gcode_handlespinupcb(s32 result)
{
	static u32 step = 0;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_handlespinupcb(%d,%d)\n",result,step);
#endif
	if(result==0x0010) {
		__gcode_statetimeout();
		return;
	}
	if(result==0x0001) {
		if(step==0x0000) {
			step++;
			_diReg[1] = _diReg[1];
			GCODE_LowEnableExtensions(__gcode_extensionsenabled,__gcode_handlespinupcb);
			return;
		}
		if(step==0x0001) {
			step++;
			_diReg[1] = _diReg[1];
			GCODE_LowSpinMotor((GCODE_SPINMOTOR_ACCEPT|GCODE_SPINMOTOR_UP),__gcode_handlespinupcb);
			return;
		}
		if(step==0x0002) {
			step = 0;
			if(!__gcode_lasterror) {
				_diReg[1] = _diReg[1];
				GCODE_LowSetStatus((_SHIFTL((GCODE_STATUS_DISK_ID_NOT_READ+1),16,8)|0x00000300),__gcode_finalsudcb);
				return;
			}
			__gcode_finalsudcb(result);
			return;
		}
	}

	step = 0;
	__gcode_stategettingerror();
}

static void __gcode_fwpatchcb(s32 result)
{
	static u32 step = 0;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_fwpatchcb(%d,%d)\n",result,step);
#endif
	if(result==0x0010) {
		__gcode_statetimeout();
		return;
	}
	if(result==0x0001) {
		if(step==0x0000) {
			step++;
			_diReg[1] = _diReg[1];
			GCODE_LowUnlockDrive(__gcode_fwpatchcb);
			return;
		}
		if(step==0x0001) {
			step = 0;
			GCODE_LowPatchDriveCode(__gcode_finalpatchcb);
			return;
		}
	}

	step = 0;
	__gcode_stategettingerror();
}

static void __gcode_checkaddonscb(s32 result)
{
	u32 txdsize;
	gcodecallbacklow cb;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_checkaddonscb(%d)\n",result);
#endif
	__gcode_drivechecked = 1;
	txdsize = (GCODE_DISKIDSIZE-_diReg[6]);
	if(result&0x0001) {
		// if the read was successful but was interrupted by a break we issue the read again.
		if(txdsize!=GCODE_DISKIDSIZE) {
			_diReg[1] = _diReg[1];
			DCInvalidateRange(&__gcode_tmpid0,GCODE_DISKIDSIZE);
			GCODE_LowReadDiskID(&__gcode_tmpid0,__gcode_checkaddonscb);
			return;
		}
		__gcode_drivestate |= GCODE_CHIPPRESENT;
	}

	cb = __gcode_finaladdoncb;
	__gcode_finaladdoncb = NULL;
	if(cb) cb(0x01);
}

static void __gcode_checkaddons(gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_checkaddons()\n");
#endif
	__gcode_finaladdoncb = cb;

	// try to read disc ID.
	_diReg[1] = _diReg[1];
	DCInvalidateRange(&__gcode_tmpid0,GCODE_DISKIDSIZE);
	GCODE_LowReadDiskID(&__gcode_tmpid0,__gcode_checkaddonscb);
	return;
}

static void __gcode_fwpatchmem(gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_fwpatchmem()\n");
#endif
	__gcode_finalpatchcb = cb;

	_diReg[1] = _diReg[1];
	DCInvalidateRange(&__gcode_driveinfo,GCODE_DRVINFSIZE);
	GCODE_LowInquiry(&__gcode_driveinfo,__gcode_fwpatchcb);
	return;
}

static void __gcode_handlespinup(void)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_handlespinup()\n");
#endif
	_diReg[1] = _diReg[1];
	GCODE_LowUnlockDrive(__gcode_handlespinupcb);
	return;
}

static void __gcode_spinupdrivecb(s32 result)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_spinupdrivecb(%d,%02x,%02x)\n",result,__gcode_resetoccured,__gcode_drivestate);
#endif
	if(result==0x0010) {
		__gcode_statetimeout();
		return;
	}
	if(result==0x0001) {
		if(!__gcode_drivechecked) {
			__gcode_checkaddons(__gcode_spinupdrivecb);
			return;
		}
		if(!(__gcode_drivestate&GCODE_CHIPPRESENT)) {
			if(!(__gcode_drivestate&GCODE_INTEROPER)) {
				if(!(__gcode_drivestate&GCODE_DRIVERESET)) {
					GCODE_Reset(GCODE_RESETHARD);
					udelay(1150*1000);
				}
				__gcode_fwpatchmem(__gcode_spinupdrivecb);
				return;
			}
			__gcode_handlespinup();
			return;
		}

		__gcode_finalsudcb(result);
		return;
	}
	__gcode_stategettingerror();
}

static void __gcode_statecoverclosed_spinupcb(s32 result)
{
	DCInvalidateRange(&__gcode_tmpid0,GCODE_DISKIDSIZE);
	__gcode_laststate = __gcode_statecoverclosed_cmd;
	__gcode_statecoverclosed_cmd(__gcode_executing);
}

static void __GCODEInterruptHandler(u32 nIrq,void *pCtx)
{
	s64 now;
	u32 status,ir,irm,irmm,diff;
	gcodecallbacklow cb;

	SYS_CancelAlarm(__gcode_timeoutalarm);

	irmm = 0;
	if(__gcode_lastcmdwasread) {
		__gcode_cmd_prev.buf = __gcode_cmd_curr.buf;
		__gcode_cmd_prev.len = __gcode_cmd_curr.len;
		__gcode_cmd_prev.offset = __gcode_cmd_curr.offset;
		if(__gcode_stopnextint) irmm |= 0x0008;
	}
	__gcode_lastcmdwasread = 0;
	__gcode_stopnextint = 0;

	status = _diReg[0];
	irm = (status&(GCODE_DE_MSK|GCODE_TC_MSK|GCODE_BRK_MSK));
	ir = ((status&(GCODE_DE_INT|GCODE_TC_INT|GCODE_BRK_INT))&(irm<<1));
#ifdef _GCODE_DEBUG
	iprintf("__GCODEInterruptHandler(status: %08x,irm: %08x,ir: %08x (%d))\n",status,irm,ir,__gcode_resetoccured);
#endif
	if(ir&GCODE_BRK_INT) irmm |= 0x0008;
	if(ir&GCODE_TC_INT) irmm |= 0x0001;
	if(ir&GCODE_DE_INT) irmm |= 0x0002;

	if(irmm) __gcode_resetoccured = 0;

	_diReg[0] = (ir|irm);

	now = gettime();
	diff = diff_msec(__gcode_lastresetend,now);
	if(__gcode_resetoccured && diff<200) {
		status = _diReg[1];
		irm = status&GCODE_CVR_MSK;
		ir = (status&GCODE_CVR_INT)&(irm<<1);
		if(ir&0x0004) {
			cb = __gcode_resetcovercb;
			__gcode_resetcovercb = NULL;
			if(cb) {
				cb(0x0004);
			}
		}
		_diReg[1] = _diReg[1];
	} else {
		if(__gcode_waitcoverclose) {
			status = _diReg[1];
			irm = status&GCODE_CVR_MSK;
			ir = (status&GCODE_CVR_INT)&(irm<<1);
			if(ir&GCODE_CVR_INT) irmm |= 0x0004;
			_diReg[1] = (ir|irm);
			__gcode_waitcoverclose = 0;
		} else
			_diReg[1] = 0;
	}

	if(irmm&0x0008) {
		if(!__gcode_breaking) irmm &= ~0x0008;
	}

	if(irmm&0x0001) {
		if(__ProcessNextCmd()) return;
	} else {
		__gcode_cmdlist[0].cmd = -1;
		__gcode_nextcmdnum = 0;
	}

	cb = __gcode_callback;
	__gcode_callback = NULL;
	if(cb) cb(irmm);

	__gcode_breaking = 0;
}

static void __gcode_patchdrivecb(s32 result)
{
	u32 cnt = 0;
	static u32 cmd_buf[3];
	static u32 stage = 0;
	static u32 nPos = 0;
	static u32 drv_address = 0x0040d000;
	const u32 chunk_size = 3*sizeof(u32);

#ifdef _GCODE_DEBUG
	iprintf("__GCODEPatchDriveCode()\n");
#endif
	if(__gcodepatchcode==NULL || __gcodepatchcode_size<=0) return;

	while(stage!=0x0003) {
		__gcode_callback = __gcode_patchdrivecb;
		if(stage==0x0000) {
#ifdef _GCODE_DEBUG
			iprintf("__GCODEPatchDriveCode(0x%08x,%02x,%d)\n",drv_address,stage,nPos);
#endif
			for(cnt=0;cnt<chunk_size && nPos<__gcodepatchcode_size;cnt++,nPos++) ((u8*)cmd_buf)[cnt] = __gcodepatchcode[nPos];

			if(nPos>=__gcodepatchcode_size) stage = 0x0002;
			else stage = 0x0001;

			_diReg[1] = _diReg[1];
			_diReg[2] = GCODE_FWWRITEMEM;
			_diReg[3] = drv_address;
			_diReg[4] = _SHIFTL(cnt,16,16);
			_diReg[7] = (GCODE_DI_DMA|GCODE_DI_START);

			drv_address += cnt;
			return;
		}

		if(stage>=0x0001) {
#ifdef _GCODE_DEBUG
			iprintf("__GCODEPatchDriveCode(%02x)\n",stage);
#endif
			if(stage>0x0001) stage = 0x0003;
			else stage = 0;

			_diReg[1] = _diReg[1];
			_diReg[2] = cmd_buf[0];
			_diReg[3] = cmd_buf[1];
			_diReg[4] = cmd_buf[2];
			_diReg[7] = GCODE_DI_START;
			return;
		}
	}
	__gcode_callback = NULL;
	__gcodepatchcode = NULL;
	__gcode_drivestate |= GCODE_INTEROPER;
	GCODE_LowFuncCall(0x0040d000,__gcode_finalpatchcb);
}


static void __gcode_unlockdrivecb(s32 result)
{
	u32 i;
#ifdef _GCODE_DEBUG
	iprintf("__GCODEUnlockDrive(%d)\n",result);
#endif
	__gcode_callback = __gcode_finalunlockcb;
	_diReg[1] = _diReg[1];
	for(i=0;i<3;i++) _diReg[2+i] = ((const u32*)__gcode_unlockcmd$222)[i];
	_diReg[7] = GCODE_DI_START;
}

static void __gcode_resetasync(gcodecbcallback cb)
{
	u32 level;

	_CPU_ISR_Disable(level);
	__gcode_clearwaitingqueue();
	if(__gcode_canceling) __gcode_cancelcallback = cb;
	else {
		if(__gcode_executing) __gcode_executing->cb = NULL;
		GCODE_CancelAllAsync(cb);
	}
	_CPU_ISR_Restore(level);
}

static void __gcode_statebusy(gcodecmdblk *block)
{
	u32 len;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_statebusy(%p)\n",block);
#endif
	__gcode_laststate = __gcode_statebusy;

	switch(block->cmd) {
		case 1:					//Read(Sector)
		case 4:
			if(!block->len) {
				block = __gcode_executing;
				__gcode_executing = &__gcode_dummycmdblk;
				block->state = GCODE_STATE_END;
				if(block->cb) block->cb(GCODE_ERROR_OK,block);
				__gcode_stateready();
				return;
			}
			_diReg[1] = _diReg[1];
			len = block->len-block->txdsize;
			if(len<0x80000) block->currtxsize = len;
			else block->currtxsize = 0x80000;
			GCODE_LowRead(block->buf+block->txdsize,block->currtxsize,(block->offset+block->txdsize),__gcode_statebusycb);
			return;
		case 2:					//Seek(Sector)
			_diReg[1] = _diReg[1];
			GCODE_LowSeek(block->offset,__gcode_statebusycb);
			return;
		case 3:
		case 15:
			GCODE_LowStopMotor(__gcode_statebusycb);
			return;
		case 5:					//ReadDiskID
			_diReg[1] = _diReg[1];
			block->currtxsize = GCODE_DISKIDSIZE;
			GCODE_LowReadDiskID(block->buf,__gcode_statebusycb);
			return;
		case 6:
			_diReg[1] = _diReg[1];
			if(__gcode_autofinishing) {
				__gcode_executing->currtxsize = 0;
				GCODE_LowRequestAudioStatus(0,__gcode_statebusycb);
			} else {
				__gcode_executing->currtxsize = 1;
				GCODE_LowAudioStream(0,__gcode_executing->len,__gcode_executing->offset,__gcode_statebusycb);
			}
			return;
		case 7:
			_diReg[1] = _diReg[1];
			GCODE_LowAudioStream(0x00010000,0,0,__gcode_statebusycb);
			return;
		case 8:
			_diReg[1] = _diReg[1];
			__gcode_autofinishing = 1;
			GCODE_LowAudioStream(0,0,0,__gcode_statebusycb);
			return;
		case 9:
			_diReg[1] = _diReg[1];
			GCODE_LowRequestAudioStatus(0,__gcode_statebusycb);
			return;
		case 10:
			_diReg[1] = _diReg[1];
			GCODE_LowRequestAudioStatus(0x00010000,__gcode_statebusycb);
			return;
		case 11:
			_diReg[1] = _diReg[1];
			GCODE_LowRequestAudioStatus(0x00020000,__gcode_statebusycb);
			return;
		case 12:
			_diReg[1] = _diReg[1];
			GCODE_LowRequestAudioStatus(0x00030000,__gcode_statebusycb);
			return;
		case 13:
			_diReg[1] = _diReg[1];
			GCODE_LowAudioBufferConfig(__gcode_executing->offset,__gcode_executing->len,__gcode_statebusycb);
			return;
		case 14:				//Inquiry
			_diReg[1] = _diReg[1];
			block->currtxsize = GCODE_DRVINFSIZE;
			GCODE_LowInquiry(block->buf,__gcode_statebusycb);
			return;
		case 16:
			_diReg[1] = _diReg[1];
			GCODE_LowStopMotor(__gcode_statebusycb);
			return;
		case 17:
			__gcode_lasterror = 0;
			__gcode_extensionsenabled = TRUE;
			GCODE_LowSpinUpDrive(__gcode_statebusycb);
			return;
		case 18:
			_diReg[1] = _diReg[1];
			GCODE_LowControlMotor(block->offset,__gcode_statebusycb);
			return;
		case 19:
			_diReg[1] = _diReg[1];
			GCODE_LowSetGCMOffset(block->offset,__gcode_statebusycb);
			return;
		case 20:
			block->currtxsize = block->len;
			GCODE_GcodeLowRead(block->buf,block->len,block->offset,__gcode_statebusycb);
			return;
		default:
			return;
	}
}

static void __gcode_stateready(void)
{
	gcodecmdblk *block;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_stateready()\n");
#endif
	if(!__gcode_checkwaitingqueue()) {
		__gcode_executing = NULL;
		return;
	}

	__gcode_executing = __gcode_popwaitingqueue();

	if(__gcode_fatalerror) {
		__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
		block = __gcode_executing;
		__gcode_executing = &__gcode_dummycmdblk;
		if(block->cb) block->cb(GCODE_ERROR_FATAL,block);
		__gcode_stateready();
		return;
	}

	__gcode_currcmd = __gcode_executing->cmd;

	if(__gcode_resumefromhere) {
		if(__gcode_resumefromhere<=7) {
			switch(__gcode_resumefromhere) {
				case 1:
					__gcode_executing->state = GCODE_STATE_WRONG_DISK;
					__gcode_statemotorstopped();
					break;
				case 2:
					__gcode_executing->state = GCODE_STATE_RETRY;
					__gcode_statemotorstopped();
					break;
				case 3:
					__gcode_executing->state = GCODE_STATE_NO_DISK;
					__gcode_statemotorstopped();
					break;
				case 4:
					__gcode_executing->state = GCODE_STATE_COVER_OPEN;
					__gcode_statemotorstopped();
					break;
				case 5:
					__gcode_executing->state = GCODE_STATE_FATAL_ERROR;
					__gcode_stateerror(__gcode_cancellasterror);
					break;
				case 6:
					__gcode_executing->state = GCODE_STATE_COVER_CLOSED;
					__gcode_statecoverclosed();
					break;
				case 7:
					__gcode_executing->state = GCODE_STATE_MOTOR_STOPPED;
					__gcode_statemotorstopped();
					break;
				default:
					break;

			}
		}
		__gcode_resumefromhere = 0;
		return;
	}
	__gcode_executing->state = GCODE_STATE_BUSY;
	__gcode_statebusy(__gcode_executing);
}

static void __gcode_statecoverclosed(void)
{
	gcodecmdblk *blk;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_statecoverclosed(%d)\n",__gcode_currcmd);
#endif
	if(__gcode_currcmd==0x0004 || __gcode_currcmd==0x0005
		|| __gcode_currcmd==0x000d || __gcode_currcmd==0x000f
		|| __gcode_currcmd==0x0011) {
		__gcode_clearwaitingqueue();
		blk = __gcode_executing;
		__gcode_executing = &__gcode_dummycmdblk;
		if(blk->cb) blk->cb(GCODE_ERROR_COVER_CLOSED,blk);
		__gcode_stateready();
	} else {
		__gcode_extensionsenabled = TRUE;
		GCODE_LowSpinUpDrive(__gcode_statecoverclosed_spinupcb);
	}
}

static void __gcode_statecoverclosed_cmd(gcodecmdblk *block)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_statecoverclosed_cmd(%d)\n",__gcode_currcmd);
#endif
	GCODE_LowReadDiskID(&__gcode_tmpid0,__gcode_statecoverclosedcb);
}

static void __gcode_statemotorstopped(void)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_statemotorstopped(%d)\n",__gcode_executing->state);
#endif
	GCODE_LowWaitCoverClose(__gcode_statemotorstoppedcb);
}

static void __gcode_stateerror(s32 result)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_stateerror(%08x)\n",result);
#endif
	__gcode_storeerror(result);
	GCODE_LowStopMotor(__gcode_stateerrorcb);
}

static void __gcode_stategettingerror(void)
{
#ifdef _GCODE_DEBUG
	iprintf("__gcode_stategettingerror()\n");
#endif
	GCODE_LowRequestError(__gcode_stategettingerrorcb);
}

static void __gcode_statecheckid2(gcodecmdblk *block)
{

}

static void __gcode_statecheckid(void)
{
	gcodecmdblk *blk;
#ifdef _GCODE_DEBUG
	iprintf("__gcode_statecheckid(%02x)\n",__gcode_currcmd);
#endif
	if(__gcode_currcmd==0x0003) {
		if(memcmp(__gcode_executing->id,&__gcode_tmpid0,GCODE_DISKIDSIZE)) {
			GCODE_LowStopMotor(__gcode_statecheckid1cb);
			return;
		}
		memcpy(__gcode_diskID,&__gcode_tmpid0,GCODE_DISKIDSIZE);

		__gcode_executing->state = GCODE_STATE_BUSY;
		DCInvalidateRange(&__gcode_tmpid0,GCODE_DISKIDSIZE);
		__gcode_laststate = __gcode_statecheckid2;
		__gcode_statecheckid2(__gcode_executing);
		return;
	}
	if(__gcode_currcmd==0x0011) {
		blk = __gcode_executing;
		blk->state = GCODE_STATE_END;
		__gcode_executing = &__gcode_dummycmdblk;
		if(blk->cb) blk->cb(GCODE_ERROR_OK,blk);
		__gcode_stateready();
		return;
	}
	if(__gcode_currcmd!=0x0005 && memcmp(__gcode_diskID,&__gcode_tmpid0,GCODE_DISKIDSIZE)) {
		blk = __gcode_executing;
		blk->state = GCODE_STATE_FATAL_ERROR;
		__gcode_executing = &__gcode_dummycmdblk;
		if(blk->cb) blk->cb(GCODE_ERROR_FATAL,blk);			//terminate current operation
		if(__gcode_mountusrcb) __gcode_mountusrcb(GCODE_DISKIDSIZE,blk);		//if we came across here, notify user callback of successful remount
		__gcode_stateready();
		return;
	}
	__gcode_statebusy(__gcode_executing);
}

static void __gcode_statetimeout(void)
{
	__gcode_storeerror(0x01234568);
	GCODE_Reset(GCODE_RESETSOFT);
	__gcode_stateerrorcb(0);
}

static void __gcode_stategotoretry(void)
{
	GCODE_LowStopMotor(__gcode_stategotoretrycb);
}

static s32 __issuecommand(s32 prio,gcodecmdblk *block)
{
	s32 ret;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("__issuecommand(%d,%p,%p)\n",prio,block,block->cb);
	iprintf("__issuecommand(%p)\n",__gcode_waitingqueue[prio].first);
#endif
	if(__gcode_autoinvalidation &&
		(block->cmd==0x0001 || block->cmd==0x00004
		|| block->cmd==0x0005 || block->cmd==0x000e
		|| block->cmd==0x0014)) DCInvalidateRange(block->buf,block->len);

	_CPU_ISR_Disable(level);
	block->state = GCODE_STATE_WAITING;
	ret = __gcode_pushwaitingqueue(prio,block);
	if(!__gcode_executing) __gcode_stateready();
	_CPU_ISR_Restore(level);
	return ret;
}

static s32 GCODE_LowUnlockDrive(gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowUnlockDrive()\n");
#endif
	u32 i;

	__gcode_callback = __gcode_unlockdrivecb;
	__gcode_finalunlockcb = cb;
	__gcode_stopnextint = 0;

	for(i=0;i<3;i++) _diReg[2+i] = ((const u32*)__gcode_unlockcmd$221)[i];
	_diReg[7] = GCODE_DI_START;

	return 1;
}

static s32 GCODE_LowPatchDriveCode(gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowPatchDriveCode(%08x)\n",__gcode_driveinfo.rel_date);
#endif
	__gcode_finalpatchcb = cb;
	__gcode_stopnextint = 0;

	if(__gcode_driveinfo.rel_date==GCODE_MODEL04) {
		__gcodepatchcode = __gcode_patchcode04;
		__gcodepatchcode_size = __gcode_patchcode04_size;
	} else if(__gcode_driveinfo.rel_date==GCODE_MODEL06) {
		__gcodepatchcode = __gcode_patchcode06;
		__gcodepatchcode_size = __gcode_patchcode06_size;
	} else if(__gcode_driveinfo.rel_date==GCODE_MODEL08) {
		__gcodepatchcode = __gcode_patchcode08;
		__gcodepatchcode_size = __gcode_patchcode08_size;
	} else if(__gcode_driveinfo.rel_date==GCODE_MODEL08Q) {
		__gcodepatchcode = __gcode_patchcodeQ08;
		__gcodepatchcode_size = __gcode_patchcodeQ08_size;
	} else {
		__gcodepatchcode = NULL;
		__gcodepatchcode_size = 0;
	}

	__gcode_patchdrivecb(0);
	return 1;
}

static s32 GCODE_LowSetOffset(s64 offset,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowSetOffset(%08llx)\n",offset);
#endif
	__gcode_stopnextint = 0;
	__gcode_callback = cb;

	_diReg[2] = GCODE_FWSETOFFSET;
	_diReg[3] = (u32)(offset>>2);
	_diReg[4] = 0;
	_diReg[7] = GCODE_DI_START;

	return 1;
}

static s32 GCODE_LowSetGCMOffset(s64 offset,gcodecallbacklow cb)
{
	static s64 loc_offset = 0;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowSetGCMOffset(%08llx)\n",offset);
#endif
	loc_offset = offset;

	__gcode_finaloffsetcb = cb;
	__gcode_stopnextint = 0;
	__gcode_usrdata = &loc_offset;

	GCODE_LowUnlockDrive(__gcode_setgcmoffsetcb);
	return 1;
}

static s32 GCODE_LowReadmem(u32 address,void *buffer,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowReadmem(0x%08x,%p)\n",address,buffer);
#endif
	__gcode_finalreadmemcb = cb;
	__gcode_usrdata = buffer;
	__gcode_callback = __gcode_readmemcb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_FWREADMEM;
	_diReg[3] = address;
	_diReg[4] = 0x00010000;
	_diReg[7] = GCODE_DI_START;

	return 1;
}

static s32 GCODE_LowFuncCall(u32 address,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowFuncCall(0x%08x)\n",address);
#endif
	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_FWFUNCCALL;
	_diReg[3] = address;
	_diReg[4] = 0x66756E63;
	_diReg[7] = GCODE_DI_START;

	return 1;
}

static s32 GCODE_LowSpinMotor(u32 mode,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowSpinMotor(%08x)\n",mode);
#endif
	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_FWCTRLMOTOR|(mode&0x0000ff00);
	_diReg[3] = 0;
	_diReg[4] = 0;
	_diReg[7] = GCODE_DI_START;

	return 1;
}

static s32 GCODE_LowEnableExtensions(u8 enable,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowEnableExtensions(%02x)\n",enable);
#endif
	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = (GCODE_FWENABLEEXT|_SHIFTL(enable,16,8));
	_diReg[3] = 0;
	_diReg[4] = 0;
	_diReg[7] = GCODE_DI_START;

	return 1;
}

static s32 GCODE_LowSetStatus(u32 status,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowSetStatus(%08x)\n",status);
#endif
	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = (GCODE_FWSETSTATUS|(status&0x00ffffff));
	_diReg[3] = 0;
	_diReg[4] = 0;
	_diReg[7] = GCODE_DI_START;

	return 1;
}

static s32 GCODE_LowGetStatus(u32 *status,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	if (status) {
		iprintf("GCODE_LowGetStatus(%08x)\n", *status);
	}
#endif
	__gcode_finalstatuscb = cb;
	__gcode_usrdata = status;

	GCODE_LowRequestError(__gcode_getstatuscb);
	return 1;
}

static s32 GCODE_LowControlMotor(u32 mode,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowControlMotor(%08x)\n",mode);
#endif
	__gcode_stopnextint = 0;
	__gcode_motorcntrl = mode;
	__gcode_finalsudcb = cb;
	GCODE_LowUnlockDrive(__gcode_cntrldrivecb);

	return 1;

}

static s32 GCODE_LowSpinUpDrive(gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowSpinUpDrive()\n");
#endif
	__gcode_finalsudcb = cb;
	__gcode_spinupdrivecb(1);

	return 1;
}

static s32 GCODE_GcodeLowRead(void *buf,u32 len,u32 offset,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_GcodeLowRead(%p,%d,%d)\n",buf,len,offset);
#endif
	struct timespec tb;

	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_GCODE_READ;
	_diReg[3] = offset;
	_diReg[4] = len;
	_diReg[5] = (u32)buf;
	_diReg[6] = len;
	_diReg[7] = (GCODE_DI_DMA|GCODE_DI_START);

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);

	return 1;
}

static s32 GCODE_LowRead(void *buf,u32 len,s64 offset,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowRead(%p,%d,%lld)\n",buf,len,offset);
#endif
	_diReg[6] = len;

	__gcode_cmd_curr.buf = buf;
	__gcode_cmd_curr.len = len;
	__gcode_cmd_curr.offset = offset;
	__DoRead(buf,len,offset,cb);
	return 1;
}

static s32 GCODE_LowSeek(s64 offset,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowSeek(%lld)\n",offset);
#endif
	struct timespec tb;

	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_SEEK;
	_diReg[3] = (u32)(offset>>2);
	_diReg[7] = GCODE_DI_START;

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);

	return 1;
}

static s32 GCODE_LowReadDiskID(gcodediskid *diskID,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowReadDiskID(%p)\n",diskID);
#endif
	struct timespec tb;

	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_READDISKID;
	_diReg[3] = 0;
	_diReg[4] = GCODE_DISKIDSIZE;
	_diReg[5] = (u32)diskID;
	_diReg[6] = GCODE_DISKIDSIZE;
	_diReg[7] = (GCODE_DI_DMA|GCODE_DI_START);

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);

	return 1;
}

static s32 GCODE_LowRequestError(gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowRequestError()\n");
#endif
	struct timespec tb;

	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_REQUESTERROR;
	_diReg[7] = GCODE_DI_START;

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);

	return 1;
}

static s32 GCODE_LowStopMotor(gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowStopMotor()\n");
#endif
	struct timespec tb;

	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_STOPMOTOR;
	_diReg[7] = GCODE_DI_START;

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);

	return 1;
}

static s32 GCODE_LowInquiry(gcodedrvinfo *info,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowInquiry(%p)\n",info);
#endif
	struct timespec tb;

	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_INQUIRY;
	_diReg[4] = GCODE_DRVINFSIZE;
	_diReg[5] = (u32)info;
	_diReg[6] = GCODE_DRVINFSIZE;
	_diReg[7] = (GCODE_DI_DMA|GCODE_DI_START);

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);

	return 1;
}

static s32 GCODE_LowWaitCoverClose(gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowWaitCoverClose()\n");
#endif
	__gcode_callback = cb;
	__gcode_waitcoverclose = 1;
	__gcode_stopnextint = 0;
	_diReg[1] = GCODE_CVR_MSK;
	return 1;
}

static s32 GCODE_LowGetCoverStatus(void)
{
	s64 now;
	u32 diff;

	now = gettime();
	diff = diff_msec(__gcode_lastresetend,now);
	if(diff<100) return 0;
	else if(_diReg[1]&GCODE_CVR_STATE) return 1;
	else return 2;
}

static void GCODE_LowReset(u32 reset_mode)
{
	u32 val;

	_diReg[1] = GCODE_CVR_MSK;
	val = _piReg[9];
	_piReg[9] = ((val&~0x0004)|0x0001);

	udelay(12);

	if(reset_mode==GCODE_RESETHARD) val |= 0x0004;
	val |= 0x0001;
	_piReg[9] = val;

	__gcode_resetoccured = 1;
	__gcode_lastresetend = gettime();
	__gcode_drivestate |= GCODE_DRIVERESET;
}

static s32 GCODE_LowAudioStream(u32 subcmd,u32 len,s64 offset,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowAudioStream(%08x,%d,%08llx,%p)\n",subcmd,len,offset,cb);
#endif
	struct timespec tb;

	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_AUDIOSTREAM|subcmd;
	_diReg[3] = (u32)(offset>>2);
	_diReg[4] = len;
	_diReg[7] = GCODE_DI_START;

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);

	return 1;
}

static s32 GCODE_LowAudioBufferConfig(s32 enable,u32 size,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowAudioBufferConfig(%02x,%d,%p)\n",enable,size,cb);
#endif
	u32 val;
	struct timespec tb;

	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	val = 0;
	if(enable) {
		val |= 0x00010000;
		if(!size) val |= 0x0000000a;
	}

	_diReg[2] = GCODE_AUDIOCONFIG|val;
	_diReg[7] = GCODE_DI_START;

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);

	return 1;
}

static s32 GCODE_LowRequestAudioStatus(u32 subcmd,gcodecallbacklow cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_LowRequestAudioStatus(%08x,%p)\n",subcmd,cb);
#endif
	struct timespec tb;

	__gcode_callback = cb;
	__gcode_stopnextint = 0;

	_diReg[2] = GCODE_AUDIOSTATUS|subcmd;
	_diReg[7] = GCODE_DI_START;

	tb.tv_sec = 10;
	tb.tv_nsec = 0;
	__SetupTimeoutAlarm(&tb);

	return 1;
}

//special, only used in bios replacement. therefor only there extern'd
s32 __GCODEAudioBufferConfig(gcodecmdblk *block,u32 enable,u32 size,gcodecbcallback cb)
{
	if(!block) return 0;

	block->cmd = 0x000d;
	block->offset = enable;
	block->len = size;
	block->cb = cb;

	return __issuecommand(2,block);
}

s32 GCODE_ReadDiskID(gcodecmdblk *block,gcodediskid *id,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_ReadDiskID(%p,%p)\n",block,id);
#endif
	if(!block || !id) return 0;

	block->cmd = 0x0005;
	block->buf = id;
	block->len = GCODE_DISKIDSIZE;
	block->offset = 0;
	block->txdsize = 0;
	block->cb = cb;

	return __issuecommand(2,block);
}

s32 GCODE_ReadAbsAsyncPrio(gcodecmdblk *block,void *buf,u32 len,s64 offset,gcodecbcallback cb,s32 prio)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_ReadAbsAsyncPrio(%p,%p,%d,%lld,%d)\n",block,buf,len,offset,prio);
#endif
	block->cmd = 0x0001;
	block->buf = buf;
	block->len = len;
	block->offset = offset;
	block->txdsize = 0;
	block->cb = cb;

	return __issuecommand(prio,block);
}

s32 GCODE_ReadAbsAsyncForBS(gcodecmdblk *block,void *buf,u32 len,s64 offset,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_ReadAbsAsyncForBS(%p,%p,%d,%lld)\n",block,buf,len,offset);
#endif
	block->cmd = 0x0004;
	block->buf = buf;
	block->len = len;
	block->offset = offset;
	block->txdsize = 0;
	block->cb = cb;

	return __issuecommand(2,block);
}

s32 GCODE_SeekAbsAsyncPrio(gcodecmdblk *block,s64 offset,gcodecbcallback cb,s32 prio)
{
	block->cmd = 0x0002;
	block->offset = offset;
	block->cb = cb;

	return __issuecommand(prio,block);
}

s32 GCODE_InquiryAsync(gcodecmdblk *block,gcodedrvinfo *info,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_InquiryAsync(%p,%p,%p)\n",block,info,cb);
#endif
	block->cmd = 0x000e;
	block->buf = info;
	block->len = GCODE_DRVINFSIZE;
	block->txdsize = 0;
	block->cb = cb;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_InquiryAsync(%p,%p)\n",block,block->cb);
#endif
	return __issuecommand(2,block);
}

s32 GCODE_Inquiry(gcodecmdblk *block,gcodedrvinfo *info)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_Inquiry(%p,%p)\n",block,info);
#endif
	ret = GCODE_InquiryAsync(block,info,__gcode_synccb);
	if(!ret) return GCODE_ERROR_FATAL;

	_CPU_ISR_Disable(level);
	do {
		state = block->state;
		if(state==GCODE_STATE_END) ret = block->txdsize;
		else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
		else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
		else LWP_ThreadSleep(__gcode_wait_queue);
	} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
	_CPU_ISR_Restore(level);

	return ret;
}

s32 GCODE_ReadPrio(gcodecmdblk *block,void *buf,u32 len,s64 offset,s32 prio)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_ReadPrio(%p,%p,%d,%lld,%d)\n",block,buf,len,offset,prio);
#endif
	if(offset>=0 && offset<8511160320LL) {
		ret = GCODE_ReadAbsAsyncPrio(block,buf,len,offset,__gcode_synccb,prio);
		if(!ret) return GCODE_ERROR_FATAL;

		_CPU_ISR_Disable(level);
		do {
			state = block->state;
			if(state==GCODE_STATE_END) ret = block->txdsize;
			else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
			else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
			else LWP_ThreadSleep(__gcode_wait_queue);
		} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
		_CPU_ISR_Restore(level);

		return ret;
	}
	return GCODE_ERROR_FATAL;
}

s32 GCODE_SeekPrio(gcodecmdblk *block,s64 offset,s32 prio)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_SeekPrio(%p,%lld,%d)\n",block,offset,prio);
#endif
	if(offset>=0 && offset<8511160320LL) {
		ret = GCODE_SeekAbsAsyncPrio(block,offset,__gcode_synccb,prio);
		if(!ret) return GCODE_ERROR_FATAL;

		_CPU_ISR_Disable(level);
		do {
			state = block->state;
			if(state==GCODE_STATE_END) ret = GCODE_ERROR_OK;
			else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
			else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
			else LWP_ThreadSleep(__gcode_wait_queue);
		} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
		_CPU_ISR_Restore(level);

		return ret;
	}
	return GCODE_ERROR_FATAL;
}

s32 GCODE_CancelAllAsync(gcodecbcallback cb)
{
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_CancelAllAsync(%p)\n",cb);
#endif
	_CPU_ISR_Disable(level);
	GCODE_Pause();
	_CPU_ISR_Restore(level);
	return 1;
}

s32 GCODE_StopStreamAtEndAsync(gcodecmdblk *block,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_StopStreamAtEndAsync(%p,%p)\n",block,cb);
#endif
	block->cmd = 0x0008;
	block->cb = cb;
	return __issuecommand(1,block);
}

s32 GCODE_StopStreamAtEnd(gcodecmdblk *block)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_StopStreamAtEnd(%p)\n",block);
#endif
	ret = GCODE_StopStreamAtEndAsync(block,__gcode_synccb);
	if(!ret) return GCODE_ERROR_FATAL;

	_CPU_ISR_Disable(level);
	do {
		state = block->state;
		if(state==GCODE_STATE_END) ret = GCODE_ERROR_OK;
		else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
		else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
		else LWP_ThreadSleep(__gcode_wait_queue);
	} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
	_CPU_ISR_Restore(level);

	return ret;
}

s32 GCODE_StopMotorAsync(gcodecmdblk *block,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_StopMotorAsync(%p,%p)\n",block,cb);
#endif
	block->cmd = 0x0010;
	block->cb = cb;
	return __issuecommand(2,block);
}

s32 GCODE_StopMotor(gcodecmdblk *block)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_StopMotor(%p)\n",block);
#endif
	ret = GCODE_StopMotorAsync(block,__gcode_synccb);
	if(!ret) return GCODE_ERROR_FATAL;

	_CPU_ISR_Disable(level);
	do {
		state = block->state;
		if(state==GCODE_STATE_END) ret = GCODE_ERROR_OK;
		else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
		else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
		else LWP_ThreadSleep(__gcode_wait_queue);
	} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
	_CPU_ISR_Restore(level);

	return ret;
}

s32 GCODE_SpinUpDriveAsync(gcodecmdblk *block,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_SpinUpDriveAsync(%p,%p)\n",block,cb);
#endif
	GCODE_Reset(GCODE_RESETNONE);

	block->cmd = 0x0011;
	block->cb = cb;
	return __issuecommand(1,block);
}

s32 GCODE_SpinUpDrive(gcodecmdblk *block)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_SpinUpDrive(%p)\n",block);
#endif
	ret = GCODE_SpinUpDriveAsync(block,__gcode_synccb);
	if(!ret) return GCODE_ERROR_FATAL;

	_CPU_ISR_Disable(level);
	do {
		state = block->state;
		if(state==GCODE_STATE_END) ret = GCODE_ERROR_OK;
		else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
		else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
		else LWP_ThreadSleep(__gcode_wait_queue);
	} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
	_CPU_ISR_Restore(level);

	return ret;
}

s32 GCODE_ControlDriveAsync(gcodecmdblk *block,u32 cmd,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_ControlMotorAsync(%p,%d,%p)\n",block,cmd,cb);
#endif
	block->cmd = 0x0012;
	block->cb = cb;
	block->offset = cmd;
	return __issuecommand(1,block);
}

s32 GCODE_ControlDrive(gcodecmdblk *block,u32 cmd)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_ControlMotor(%p,%d)\n",block,cmd);
#endif
	ret = GCODE_ControlDriveAsync(block,cmd,__gcode_synccb);
	if(!ret) return GCODE_ERROR_FATAL;

	_CPU_ISR_Disable(level);
	do {
		state = block->state;
		if(state==GCODE_STATE_END) ret = GCODE_ERROR_OK;
		else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
		else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
		else LWP_ThreadSleep(__gcode_wait_queue);
	} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
	_CPU_ISR_Restore(level);

	return ret;
}

s32 GCODE_SetGCMOffsetAsync(gcodecmdblk *block,s64 offset,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_SetGCMOffsetAsync(%p,%08llx,%p)\n",block,offset,cb);
#endif
	block->cmd = 0x0013;
	block->cb = cb;
	block->offset = offset;
	return __issuecommand(1,block);
}

s32 GCODE_SetGCMOffset(gcodecmdblk *block,s64 offset)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_SetGCMOffset(%p,%08llx)\n",block,offset);
#endif
	ret = GCODE_SetGCMOffsetAsync(block,offset,__gcode_synccb);
	if(!ret) return GCODE_ERROR_FATAL;

	_CPU_ISR_Disable(level);
	do {
		state = block->state;
		if(state==GCODE_STATE_END) ret = GCODE_ERROR_OK;
		else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
		else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
		else LWP_ThreadSleep(__gcode_wait_queue);
	} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
	_CPU_ISR_Restore(level);

	return ret;
}

s32 GCODE_GcodeReadAsync(gcodecmdblk *block,void *buf,u32 len,u32 offset,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_GcodeReadAsync(%p,%p,%d,%d)\n",block,buf,len,offset);
#endif
	block->cmd = 0x0014;
	block->buf = buf;
	block->len = len;
	block->offset = offset;
	block->txdsize = 0;
	block->cb = cb;

	return __issuecommand(2,block);
}

s32 GCODE_GcodeRead(gcodecmdblk *block,void *buf,u32 len,u32 offset)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_GcodeRead(%p,%p,%d,%d)\n",block,buf,len,offset);
#endif
	ret = GCODE_GcodeReadAsync(block,buf,len,offset,__gcode_synccb);
	if(!ret) return GCODE_ERROR_FATAL;

	_CPU_ISR_Disable(level);
	do {
		state = block->state;
		if(state==GCODE_STATE_END) ret = block->txdsize;
		else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
		else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
		else LWP_ThreadSleep(__gcode_wait_queue);
	} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
	_CPU_ISR_Restore(level);

	return ret;
}

s32 GCODE_GetCmdBlockStatus(gcodecmdblk *block)
{
	s32 ret;
	u32 level;

	_CPU_ISR_Disable(level);
	if((ret=block->state)==GCODE_STATE_COVER_CLOSED) ret = GCODE_STATE_BUSY;
	_CPU_ISR_Restore(level);
	return ret;
}

s32 GCODE_GetDriveStatus(void)
{
	s32 ret;
	u32 level;

	_CPU_ISR_Disable(level);
	if(__gcode_fatalerror) ret = GCODE_STATE_FATAL_ERROR;
	else {
		if(__gcode_pausingflag) ret = GCODE_STATE_PAUSING;
		else {
			if(!__gcode_executing || __gcode_executing==&__gcode_dummycmdblk) ret = GCODE_STATE_END;
			else ret = GCODE_GetCmdBlockStatus(__gcode_executing);
		}
	}
	_CPU_ISR_Restore(level);
	return ret;
}

void GCODE_Pause(void)
{
	u32 level;

	_CPU_ISR_Disable(level);
	__gcode_pauseflag = 1;
	if(__gcode_executing==NULL) __gcode_pausingflag = 1;
	_CPU_ISR_Restore(level);
}

void GCODE_Reset(u32 reset_mode)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_Reset(%d)\n",reset_mode);
#endif
	__gcode_drivestate &= ~(GCODE_INTEROPER|GCODE_CHIPPRESENT|GCODE_DRIVERESET);

	if(reset_mode!=GCODE_RESETNONE)
		GCODE_LowReset(reset_mode);

	_diReg[0] = (GCODE_DE_MSK|GCODE_TC_MSK|GCODE_BRK_MSK);
	_diReg[1] = _diReg[1];

	__gcode_resetrequired = 0;
	__gcode_internalretries = 0;
}

static void callback(s32 result,gcodecmdblk *block)
{
#ifdef _GCODE_DEBUG
	iprintf("callback(%d)\n",result);
#endif
	if(result==GCODE_ERROR_OK) {
		GCODE_ReadDiskID(block,&__gcode_tmpid0,callback);
		return;
	}
	else if(result>=GCODE_DISKIDSIZE) {
		memcpy(__gcode_diskID,&__gcode_tmpid0,GCODE_DISKIDSIZE);
	} else if(result==GCODE_ERROR_COVER_CLOSED) {
		GCODE_SpinUpDriveAsync(block,callback);
		return;
	}
	if(__gcode_mountusrcb) __gcode_mountusrcb(result,block);
}

s32 GCODE_MountAsync(gcodecmdblk *block,gcodecbcallback cb)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_MountAsync()\n");
#endif
	__gcode_mountusrcb = cb;
	GCODE_Reset(GCODE_RESETHARD);
	udelay(1150*1000);
	return GCODE_SpinUpDriveAsync(block,callback);
}

s32 GCODE_Mount(void)
{
	s32 ret,state;
	u32 level;
#ifdef _GCODE_DEBUG
	iprintf("GCODE_Mount()\n");
#endif
	ret = GCODE_MountAsync(&__gcode_block$15,__gcode_synccb);
	if(!ret) return GCODE_ERROR_FATAL;

	_CPU_ISR_Disable(level);
	do {
		state = __gcode_block$15.state;
		if(state==GCODE_STATE_END) ret = GCODE_ERROR_OK;
		else if(state==GCODE_STATE_FATAL_ERROR) ret = GCODE_ERROR_FATAL;
		else if(state==GCODE_STATE_CANCELED) ret = GCODE_ERROR_CANCELED;
		else LWP_ThreadSleep(__gcode_wait_queue);
	} while(state!=GCODE_STATE_END && state!=GCODE_STATE_FATAL_ERROR && state!=GCODE_STATE_CANCELED);
	__gcode_mountusrcb = NULL;		//set to zero coz this is only used to sync for this function.
	_CPU_ISR_Restore(level);

	return ret;
}

gcodediskid* GCODE_GetCurrentDiskID(void)
{
	return __gcode_diskID;
}

gcodedrvinfo* GCODE_GetDriveInfo(void)
{
	return &__gcode_driveinfo;
}

void GCODE_Init(void)
{
#ifdef _GCODE_DEBUG
	iprintf("GCODE_Init()\n");
#endif
	if(!__gcode_initflag) {
		__gcode_initflag = 1;
		__gcode_clearwaitingqueue();
		__GCODEInitWA();

		IRQ_Request(IRQ_PI_DI,__GCODEInterruptHandler,NULL);
		__UnmaskIrq(IRQMASK(IRQ_PI_DI));

		SYS_CreateAlarm(&__gcode_timeoutalarm);
		LWP_InitQueue(&__gcode_wait_queue);

		_diReg[0] = (GCODE_DE_MSK|GCODE_TC_MSK|GCODE_BRK_MSK);
		_diReg[1] = 0;
	}
}

u32 GCODE_SetAutoInvalidation(u32 auto_inv)
{
	u32 ret = __gcode_autoinvalidation;
	__gcode_autoinvalidation= auto_inv;
	return ret;
}

static bool __gcgcode_Startup(void)
{
	GCODE_Init();

	if (mfpvr() == 0x00083214) // GameCube
	{
		GCODE_Mount();
	}
	else
	{
		GCODE_Reset(GCODE_RESETHARD);
		GCODE_ReadDiskID(&__gcode_block$15, &__gcode_tmpid0, callback);
	}
	return true;
}

static bool __gcgcode_IsInserted(void)
{
	u32 status = 0;
	GCODE_LowGetStatus(&status, NULL);

	if(GCODE_STATUS(status) == GCODE_STATUS_READY) 
		return true;

	return false;
}

static bool __gcgcode_ReadSectors(sec_t sector,sec_t numSectors,void *buffer)
{
	gcodecmdblk blk;

	if(GCODE_ReadPrio(&blk, buffer, numSectors << 11, sector << 11, 2) < 0)
		return false;

	return true;
}

static bool __gcgcode_WriteSectors(sec_t sector,sec_t numSectors,const void *buffer)
{
	return false;
}

static bool __gcgcode_ClearStatus(void)
{
	return true;
}

static bool __gcgcode_Shutdown(void)
{
	gcodecmdblk blk;
	GCODE_StopMotor(&blk);
	return true;
}

static bool __gcode_Startup(void)
{
	gcodecmdblk blk;

	GCODE_Init();
	GCODE_Inquiry(&blk, &__gcode_driveinfo);

	if(__gcode_driveinfo.rel_date != 0x20196c64)
		return false;

	return true;
}

static bool __gcode_IsInserted(void)
{
	if(GCODE_LowGetCoverStatus() == 1)
		return false;

	return true;
}

static bool __gcode_ReadSectors(sec_t sector,sec_t numSectors,void *buffer)
{
	gcodecmdblk blk;

	if(GCODE_GcodeRead(&blk, buffer, numSectors << 9, sector) < 0)
		return false;

	return true;
}

static bool __gcode_WriteSectors(sec_t sector,sec_t numSectors,const void *buffer)
{
	return false;
}

static bool __gcode_ClearStatus(void)
{
	return true;
}

static bool __gcode_Shutdown(void)
{
	return true;
}

const DISC_INTERFACE __io_gcode = {
	DEVICE_TYPE_GAMECUBE_GCODE,
	FEATURE_MEDIUM_CANREAD,
	__gcode_Startup,
	__gcode_IsInserted,
	__gcode_ReadSectors,
	__gcode_WriteSectors,
	__gcode_ClearStatus,
	__gcode_Shutdown
};
