// ipl loading
#include <ogc/machine/processor.h>
#include <ogcsys.h>
#include <gccore.h>
#include <unistd.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "sd.h"

#include "print.h"
#include "halt.h"

#include "tinf_crc32.h"
#include "descrambler.h"
#include "crc32.h"
#include "ipl.h"

#include "flippy_sync.h"
#include "boot/ssaram.h"

extern GXRModeObj *rmode;
extern void *xfb;

#define IPL_ROM_FONT_SJIS	0x1AFF00
#define DECRYPT_START		0x100

#define IPL_SIZE 0x200000
#define BS2_START_OFFSET 0x800
#define BS2_CODE_OFFSET (BS2_START_OFFSET + 0x20)
#define BS2_BASE_ADDR 0x81300000

// #define DISABLE_SDA_CHECK

// SDA finding
#ifndef DISABLE_SDA_CHECK
#define CLEAR32_INST_CNT 1
#define LOAD32_INST_CNT 2
#define INST_SIZE 4

#define NUM_GPR 32
#define NUM_RESERVED_GPR 3
#define NUM_GPR_CLEARS (INST_SIZE * (CLEAR32_INST_CNT * (NUM_GPR - NUM_RESERVED_GPR)))
#define SDA_LOAD_OFFSET (INST_SIZE * (LOAD32_INST_CNT * 2))

#define STACK_SETUP_ADDR 0x81300098
#define SDA_LOAD_ADDR_A (STACK_SETUP_ADDR + SDA_LOAD_OFFSET)
#define SDA_LOAD_ADDR_B (SDA_LOAD_ADDR_A + NUM_GPR_CLEARS)
#endif

ATTRIBUTE_ALIGN(32) static u8 bios_buffer[IPL_SIZE];

static u32 bs2_size = IPL_SIZE - BS2_CODE_OFFSET;
static u8 *bs2 = (u8*)(BS2_BASE_ADDR);

s8 bios_index = -1;
bios_item_t *current_bios;

#ifdef TEST_IPL_PATH
char *bios_path = TEST_IPL_PATH;
#else
char *bios_path = "/ipl.bin";
#endif

// NOTE: these are not ipl.bin CRCs, but decoded ipl[0x100:] hashes
// FIXME: this is over-reading by a lot (not fixed to code size)
bios_item_t bios_table[] = {
    {IPL_NTSC_10,      IPL_NTSC,  "gc-ntsc-10",      "ntsc10",       "VER_NTSC_10",      CLEANCRC(0xbb05745c), DIRTYCRC(0x2487919c), SDA(0x81465320)},
    {IPL_NTSC_11,      IPL_NTSC,  "gc-ntsc-11",      "ntsc11",       "VER_NTSC_11",      CLEANCRC(0x6cd1acb5), DIRTYCRC(0x53faeffc), SDA(0x81489120)},
    {IPL_NTSC_12_001,  IPL_NTSC,  "gc-ntsc-12_001",  "ntsc12_001",   "VER_NTSC_12_001",  CLEANCRC(0xedf79188), DIRTYCRC(0xdc527533), SDA(0x8148b1c0)},
    {IPL_NTSC_12_101,  IPL_NTSC,  "gc-ntsc-12_101",  "ntsc12_101",   "VER_NTSC_12_101",  CLEANCRC(0x65434097), DIRTYCRC(0x84075b6d), SDA(0x8148b640)},
    {IPL_PAL_10,       IPL_PAL,   "gc-pal-10",       "pal10",        "VER_PAL_10",       CLEANCRC(0x9391a4d2), DIRTYCRC(0xeadf6ce5), SDA(0x814b4fc0)},
    {IPL_PAL_11,       IPL_PAL,   "gc-pal-11",       "pal11",        "VER_PAL_11",       CLEANCRC(0x3e9af028), DIRTYCRC(0x76b301de), SDA(0x81483de0)}, // MPAL
    {IPL_PAL_12,       IPL_PAL,   "gc-pal-12",       "pal12",        "VER_PAL_12",       CLEANCRC(0xa53af44f), DIRTYCRC(0x89f5a81a), SDA(0x814b7280)},
};

extern void __SYS_ReadROM(void *buf,u32 len,u32 offset);

extern u64 gettime(void);
extern u32 diff_msec(s64 start,s64 end);

static bool valid = false;

void post_ipl_loaded() {
    iprintf("IPL index = %d\n", bios_index);

    current_bios = &bios_table[bios_index];
    iprintf("IPL %s loaded...\n", current_bios->name);

    if (current_bios->type == IPL_PAL && VIDEO_GetCurrentTvMode() == VI_NTSC) {
        iprintf("Switching to VI to PAL\n");
        if (current_bios->version == IPL_PAL_11) {
            rmode = &TVPal528IntDf;
        } else {
            rmode = &TVMpal480IntDf;
        }
        VIDEO_Configure(rmode);
        VIDEO_SetNextFramebuffer(xfb);
        VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
        VIDEO_SetBlack(FALSE);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
    }
}

void load_ipl_rom(bool is_running_dolphin) {
    if (is_running_dolphin) {
        __SYS_ReadROM(bs2, bs2_size, BS2_CODE_OFFSET); // IPL is not encrypted on Dolphin
        iprintf("TEST IPL D, %08x\n", *(u32*)bs2);
    } else {
        iprintf("TEST IPL X\n");
        __SYS_ReadROM(bios_buffer, IPL_SIZE, 0);

        iprintf("TEST IPL A, %08x\n", *(u32*)bios_buffer);
        iprintf("TEST IPL C, %08x\n", *(u32*)(bios_buffer + DECRYPT_START));
        Descrambler(bios_buffer + DECRYPT_START, IPL_ROM_FONT_SJIS - DECRYPT_START);
        memcpy(bs2, bios_buffer + BS2_CODE_OFFSET, bs2_size);
        iprintf("TEST IPL D, %08x\n", *(u32*)bs2);
    }
}

void load_ipl_file() {
    int size = get_file_size(bios_path);
    if (size == SD_FAIL) {
        char err_buf[255];
        sprintf(err_buf, "Failed to find %s\n", bios_path);
        prog_halt(err_buf);
        return;
    }

    if (size != IPL_SIZE) {
        char err_buf[255];
        sprintf(err_buf, "File %s is the wrong size %x\n", bios_path, size);
        prog_halt(err_buf);
        return;
    }

    if (load_file_buffer(bios_path, bios_buffer)) {
        char err_buf[255];
        sprintf(err_buf, "Failed to load %s\n", bios_path);
        prog_halt(err_buf);
        return;
    }

    Descrambler(bios_buffer + DECRYPT_START, IPL_ROM_FONT_SJIS - DECRYPT_START);
    memcpy(bs2, bios_buffer + BS2_CODE_OFFSET, bs2_size);

    u32 sda = get_sda_address();
    iprintf("Read IPL sda=%08x\n", sda);

    u32 crc = csp_crc32_memory(bs2, bs2_size);
    iprintf("Read IPL crc=%08x\n", crc);

#ifdef FORCE_IPL_LOAD
    // temp before adding back IPL files
    for(int i = 0; i < sizeof(bios_table) / sizeof(bios_table[0]); i++) {
        bios_table[i].dirty_crc = crc;
    }
#endif

    valid = false;
    for(int i = 0; i < sizeof(bios_table) / sizeof(bios_table[0]); i++) {
        if(bios_table[i].dirty_crc == crc && bios_table[i].sda == sda) {
            bios_index = i;
            valid = true;
            break;
        }
    }

    if (!valid) {
        prog_halt("Bad IPL image\n");
    }
}

void load_ipl(bool is_running_dolphin) {
    ipl_metadata_t *blob_metadata = (void*)0x81500000 - sizeof(ipl_metadata_t);

    iprintf("ORIG Metadata:\n");
    iprintf("\tMagic: %x\n", blob_metadata->magic);
    iprintf("\tRevision: %x\n", blob_metadata->revision);
    iprintf("\tBlob checksum: %x\n", blob_metadata->blob_checksum);
    iprintf("\tCode size: %x\n", blob_metadata->code_size);
    iprintf("\tCode checksum: %x\n", blob_metadata->code_checksum);

    ARAMFetch((void*)BS2_BASE_ADDR, (void*)0xe00000, 0x200000);

    ipl_metadata_t metadata = {};
    memcpy(&metadata, blob_metadata, sizeof(ipl_metadata_t));
    memset(blob_metadata, 0, sizeof(ipl_metadata_t));

    iprintf("ARAM Metadata:\n");
    iprintf("\tMagic: %x\n", metadata.magic);
    iprintf("\tRevision: %x\n", metadata.revision);
    iprintf("\tBlob checksum: %x\n", metadata.blob_checksum);
    iprintf("\tCode size: %x\n", metadata.code_size);
    iprintf("\tCode checksum: %x\n", metadata.code_checksum);

#ifndef FORCE_IPL_LOAD
    if (metadata.magic != 0xC0DE) {
        iprintf("Invalid IPL metadata\n"); // do not halt, we can recover
        if (is_running_dolphin) {
            prog_halt("Invalid IPL metadata\n");
        } else {
            exit(0); // call stub
        }
    }
#endif

    u32 sda = get_sda_address();
    iprintf("Read BS2 sda=%08x\n", sda);

    u32 crc = tinf_crc32((void*)BS2_BASE_ADDR, metadata.code_size);
    iprintf("Read BS2 crc=%08x\n", crc);

    valid = false;
    for(int i = 0; i < sizeof(bios_table) / sizeof(bios_table[0]); i++) {
        if(bios_table[i].dirty_crc == crc && bios_table[i].sda == sda) {
            bios_index = i;
            valid = true;
            break;
        }
    }

#ifdef FORCE_IPL_LOAD
    // TEST ONLY
    valid = false;
#endif

    iprintf("BS2 is valid? = %d\n", valid);
    if (!valid) {
        iprintf("FALLBACK: loading IPL from %s\n", bios_path);
        load_ipl_file();
    }

    post_ipl_loaded();
    return;
}

u32 get_sda_address() {
    u32 *sda_load = (u32*)SDA_LOAD_ADDR_A;
    if (*(u32*)STACK_SETUP_ADDR == 0x38000000) {
        sda_load = (u32*)SDA_LOAD_ADDR_B;
    }
    u32 sda_high = (sda_load[0] & 0xFFFF) << 16;
    u32 sda_low = sda_load[1] & 0xFFFF;
    u32 sda = sda_high | sda_low;
    return sda;
}
