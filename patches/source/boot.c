#include <memory.h>

#include "flippy_sync.h"
#include "reloc.h"
#include "attr.h"

#include "dolphin_os.h"
#include "video.h"

#include "time.h"
#include "picolibc.h"
#include "boot.h"
#include "dol.h"

#include "games.h"

#include "dolphin_dvd.h"
#include "gc_dvd.h"

// cubeboot_vmc: virtual memory cards (vmc/, docs/virtual_memory_card.md)
#include "vmc/cbvmc_loader.h"
#include "vmc/cbvmc_flash_hook.h"
#include "vmc/cbvmc_ipl_plan.h"
extern uint32_t cbvmc_ipl_diag;                  // vmc_diag (cbvmc_flash_hook.c)
#include "vmc/cbvmc_scan.h"
#include "cbvmc_resident_bin.h"
#include "cbvmc_install_bin.h"

#define SYS_VIDEO_NTSC   0
#define SYS_VIDEO_PAL    1
#define SYS_VIDEO_MPAL   2

#define TB_BUS_CLOCK     162000000u
#define TB_CORE_CLOCK    486000000u

void load_stub() {
    custom_OSReport("Loading stub...\n");
    dvd_custom_open_flash("/stub.bin", FILE_ENTRY_TYPE_FILE, 0);
    file_status_t *file_status = dvd_custom_status();
    if (file_status == NULL || file_status->result != 0) {
        custom_OSReport("Failed to open stub\n");
        return;
    }

    u32 file_size = (u32)__builtin_bswap64(*(u64*)(&file_status->fsize));
    file_size += 31;
    file_size &= 0xffffffe0;

    dvd_read((void*)STUB_ADDR, file_size, 0, file_status->fd);
    ICInvalidateRange((void*)STUB_ADDR, file_size);

    dvd_custom_close(file_status->fd);
    return;
}

__attribute__((aligned(32))) static DOLHEADER dol_hdr;
static dol_info_t load_dol(uint64_t offset, uint8_t fd) {
    DOLHEADER *hdr = &dol_hdr;
    dvd_read(hdr, sizeof(DOLHEADER), offset, fd);

    // Clear BSS
    // TODO: check if this overlaps with IPL?
    if (hdr->bssAddress && hdr->bssLength) {
        custom_OSReport("Clearing BSS %08x - %08x...\n", hdr->bssAddress, hdr->bssAddress + hdr->bssLength);
        memset((void*)hdr->bssAddress, 0, hdr->bssLength);
    }

    // Inspect text sections to see if what we found lies in here
    for (int i = 0; i < MAXTEXTSECTION; i++) {
        if (hdr->textAddress[i] && hdr->textLength[i]) {
            dvd_read_data((void*)hdr->textAddress[i], hdr->textLength[i], offset + hdr->textOffset[i], fd);
        }
    }

    // Inspect data sections (shouldn't really need to unless someone was sneaky..)
    for (int i = 0; i < MAXDATASECTION; i++) {
        if (hdr->dataAddress[i] && hdr->dataLength[i]) {
            dvd_read_data((void*)hdr->dataAddress[i], hdr->dataLength[i], offset + hdr->dataOffset[i], fd);
        }
    }

    custom_OSReport("Copy done...\n");

    void *entrypoint = (void*)hdr->entryPoint;
    u32 dol_max = DOLMax(hdr);

    dol_info_t info = {dol_max, entrypoint};
    return info;
}


dol_info_t load_dol_file(char *path, bool flash) {
    uint8_t flags = IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLEFASTSEEK | IPC_FILE_FLAG_DISABLESPEEDEMU;
    if (flash) {
        dvd_custom_open_flash(path, FILE_ENTRY_TYPE_FILE, flags);
    } else {
        dvd_custom_open(path, FILE_ENTRY_TYPE_FILE, flags);
    }

    file_status_t *file_status = dvd_custom_status();
    if (file_status == NULL || file_status->result != 0) {
        custom_OSReport("Failed to open file %s\n", path);
        return (dol_info_t){0, 0};
    }

    dol_info_t info = load_dol(0, file_status->fd);
    dvd_custom_close(file_status->fd);

    return info;
}

void chainload_swiss_game(char *game_path, bool passthrough) {
    dol_info_t info = load_dol_file("/swiss-gc.dol", true);

    char *argz = (void*)info.max_addr + 32;
    int argz_len = 0;

    char autoload_arg[256];
    if (passthrough) {
        strcpy(autoload_arg, "Autoload=dvd:/*.gcm");
    } else {
        strcpy(autoload_arg, "Autoload=fldrv:");
        strcat(autoload_arg, game_path);
    }

    const char *arg_list[] = {
        "swiss-novideo.dol", // this causes Swiss to black the screen
        autoload_arg,
        "AutoBoot=Yes",
        "IGRType=Reboot",
        "BS2Boot=No",
        "Prefer Clean Boot=No",
        NULL
    };

    int arg_count = 0;
    while(arg_list[arg_count] != NULL) {
        const char *arg = arg_list[arg_count];
        int arg_len = strlen(arg) + 1;
        memcpy(argz + argz_len, arg, arg_len);
        argz_len += arg_len;
        arg_count++;
    }

    struct __argv *args = (void*)(info.entrypoint + 8);
    args->argvMagic = ARGV_MAGIC;
    args->commandLine = argz;
    args->length = argz_len;

    DCFlushRange(argz, argz_len);
    DCFlushRange(args, sizeof(struct __argv));

    run(info.entrypoint);
}

const char GXPeekARGBPatch_bin[0x1c] = { // PIC
    0x3c, 0xc0, 0xc8, 0x00,    // 	lis		%r6, 0xC800
    0x50, 0x66, 0x15, 0x3a,    // 	insrwi	%r6, %r3, 10, 20
    0x50, 0x86, 0x62, 0xa6,    // 	insrwi	%r6, %r4, 10, 10
    0x7c, 0x00, 0x04, 0xac,    // 	sync
    0x80, 0x06, 0x00, 0x00,    // 	lwz		%r0, 0 (%r6)
    0x90, 0x05, 0x00, 0x00,    // 	stw		%r0, 0 (%r5)
    0x4e, 0x80, 0x00, 0x20,    // 	blr
};

const int GXPeekARGBPatch_length = 0x1c;

const char GXPokeARGBPatch_bin[0x18] = { // PIC
    0x3c, 0xc0, 0xc8, 0x00,    // 	lis		%r6, 0xC800
    0x50, 0x66, 0x15, 0x3a,    // 	insrwi	%r6, %r3, 10, 20
    0x50, 0x86, 0x62, 0xa6,    // 	insrwi	%r6, %r4, 10, 10
    0x7c, 0x00, 0x04, 0xac,    // 	sync
    0x90, 0xa6, 0x00, 0x00,    // 	stw		%r5, 0 (%r6)
    0x4e, 0x80, 0x00, 0x20,    // 	blr
};

const int GXPokeARGBPatch_length = 0x18;

// cubeboot_vmc: best-effort write of the scan report to /cubeboot/vmc_scan.txt (one file, replaced each boot). Any failure is ignored.
static void cbvmc_write_report(char *text, u32 len) {
    const char *path = "/cubeboot/vmc_scan.txt";
    dvd_custom_unlink((char*)path);
    if (dvd_custom_open(path, FILE_ENTRY_TYPE_FILE, IPC_FILE_FLAG_WRITE | IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLEFASTSEEK | IPC_FILE_FLAG_DISABLESPEEDEMU) != 0) return;
    file_status_t *st = dvd_custom_status();
    if (st == NULL || st->result != 0) return;
    DCFlushRange(text, len);
    dvd_custom_write(text, 0, len, st->fd);
    dvd_custom_close(st->fd);
}

extern const void _patches_end;

void chainload_boot_game(gm_file_entry_t *boot_entry, bool passthrough) {
    u32 patchSize = 0x200; // setup patching space
    u32 topAddr = 0x81800000 - patchSize;

    memset((void*)0x80000000, 0, 0x100);
    memset((void*)0x80000000, 0 ,0x100);

    // lowmem
    *(vu32*)(0x80000028) = 0x01800000;
	*(vu32*)(0x8000002C) = 0x00000001;
	*(vu32*)(0x800000CC) = SYS_VIDEO_NTSC; // overwrite with tv_mode from BI2
	*(vu32*)(0x800000D0) = 0x01000000;
	*(vu32*)(0x800000E8) = 0x81800000 - topAddr;
	*(vu32*)(0x800000EC) = topAddr;
	*(vu32*)(0x800000F0) = 0x01800000;
	*(vu32*)(0x800000F8) = TB_BUS_CLOCK;
	*(vu32*)(0x800000FC) = TB_CORE_CLOCK;

    u32 dol_offset = 0;
    u32 fst_offset = 0;
	u32 fst_size = 0;
	u32 max_fst_size = 0;

    if (!passthrough) {
        // open file
        dvd_custom_open(boot_entry->path, FILE_ENTRY_TYPE_FILE, 0);
        file_status_t *file_status = dvd_custom_status();
        if (file_status == NULL || file_status->result != 0) {
            custom_OSReport("Failed to open file %s\n", boot_entry->path);
            return;
        }

        uint32_t first_fd = file_status->fd;
        uint32_t second_fd = 0;
        custom_OSReport("Boot entry: %s\n", boot_entry->path);
        if (boot_entry->second != NULL) {
            custom_OSReport("Second entry: %s (valid: %d, %p)\n", boot_entry->second->path, boot_entry->second->second == boot_entry, boot_entry->second);
            dvd_custom_open(boot_entry->second->path, FILE_ENTRY_TYPE_FILE, 0);
            file_status_t *file_status = dvd_custom_status();
            if (file_status == NULL || file_status->result != 0) {
                custom_OSReport("Failed to open file %s\n", boot_entry->second->path);
                return;
            }
            second_fd = file_status->fd;
        }
        dvd_set_default_fd(first_fd, second_fd);

        // Read the game header to 0x80000000
        dvd_read((void*)0x80000000, 0x20, 0, 0);

        dol_offset = boot_entry->extra.dvd_dol_offset;
        fst_offset = boot_entry->extra.dvd_fst_offset;
        fst_size = boot_entry->extra.dvd_fst_size;
        max_fst_size = boot_entry->extra.dvd_max_fst_size;
    } else {
        __attribute__((aligned(32))) static DiskHeader header;
        dvd_read(&header, sizeof(DiskHeader), 0, 0); //Read in the disc header
        memcpy((void*)0x80000000, &header, 0x20);
        DCFlushRange((void*)0x80000000, 0x20);

        dol_offset = header.DOLOffset;
        fst_offset = header.FSTOffset;
        fst_size = header.FSTSize;
        max_fst_size = header.MaxFSTSize;
    }

    // cubeboot_vmc: every disc started by Cubeboot's own disc-image boot (this function; no game-id gate since T2, the generic scanner decides); a Swiss launch
    // (chainload_swiss_game) and DOL launches never come here, so Swiss keeps its own virtual card. The EXI slots are probed first: a physical memory
    // card (or any other device) in a slot always wins, and with both slots taken nothing at all is reserved or touched (stock behaviour, byte for byte).
    // Otherwise take the RESIDENT image's memory (CBVMC_R_SIZE) out of the top of MEM1 before the FST is placed. It sits above the FST and the arena,
    // so the game never sees or clears it; the one-shot install image is staged inside the arena and costs the game nothing (section 11 of the design note).
    u32 cbvmc_allow = 0;
    bool cbvmc_wanted = false;
    {
        cbvmc_allow = cbvmc_flash_enabled() ? cbvmc_flash_probe() : 0;      // vmc = 0 in config.ini switches the whole thing off (stock behaviour)
        cbvmc_wanted = cbvmc_allow != 0;
        custom_OSReport("cbvmc: slot probe allow=%d\n", cbvmc_allow);
    }
    if (cbvmc_wanted) {
        patchSize += CBVMC_R_SIZE;
        topAddr = 0x81800000 - patchSize;
        *(vu32*)(0x800000E8) = 0x81800000 - topAddr;
        *(vu32*)(0x800000EC) = topAddr;
    }

    // print all offsets and sizes
    custom_OSReport("DOL Offset: %08x\n", dol_offset);
    custom_OSReport("FST Offset: %08x\n", fst_offset);
    custom_OSReport("FST Size: %08x\n", fst_size);
    custom_OSReport("Max FST Size: %08x\n", max_fst_size);

    // Read FST to top of Main Memory (round to 32 byte boundary)
    u32 fstAddr = (topAddr-max_fst_size)&~31;
    custom_OSReport("READING: FST Address: %08x\n", fstAddr);
    u32 fstSize = (fst_size+31)&~31;
    int ret = dvd_read((void*)fstAddr, fstSize, fst_offset, 0);
    if (ret != 0) {
        custom_OSReport("Failed to read FST!\n");
        return;
    }

    // Copy bi2.bin (Disk Header Information) to just under the FST
    u32 bi2Addr = (fstAddr-0x2000)&~31;
    ret = dvd_read((void*)bi2Addr, 0x2000, 0x440, 0);
    if (ret != 0) {
        custom_OSReport("Failed to read BI2!\n");
        return;
    }

    custom_OSReport("BI2 Address: %08x\n", bi2Addr);

    // Patch bi2.bin
    DiskHeaderInformation *bi2 = (DiskHeaderInformation*)bi2Addr;
    bi2->SimMemSize = 0x1800000;

    // // test only
    // bi2->DebugFlag = 1;

    *(vu32*)(0x80000034) = fstAddr;       // Arena Hi
    *(vu32*)(0x80000038) = fstAddr;       // FST Location in ram
    *(vu32*)(0x8000003C) = max_fst_size;  // FST Max Length
    *(vu32*)(0x800000F4) = bi2Addr;

    custom_OSReport("BI2: %08x\n", bi2);
    custom_OSReport("Country: %x\n", bi2->RegionCode);

    // game id
    struct dolphin_lowmem *lowmem = (struct dolphin_lowmem*)0x80000000;
    custom_OSReport("Game ID: %c%c%c%c\n", lowmem->b_disk_info.game_code[0], lowmem->b_disk_info.game_code[1], lowmem->b_disk_info.game_code[2], lowmem->b_disk_info.game_code[3]);

    if (bi2->RegionCode == COUNTRY_EUR) {
        custom_OSReport("PAL game detected\n");
        ogc__VIInit(VI_TVMODE_PAL_INT);

        // set video mode PAL
        u32 mode = rmode->viTVMode >> 2;
        if (mode == VI_MPAL) {
            lowmem->tv_mode = 5;
        } else {
            lowmem->tv_mode = 1;
        }
    } else {
        custom_OSReport("NTSC game detected\n");
        ogc__VIInit(VI_TVMODE_NTSC_INT);

        lowmem->tv_mode = 0;
    }

    if (dol_offset == 0) {
        dol_offset = get_dol_iso9660(0);
        if (dol_offset) {
            // memset((void*)0x80000000, 0, 0x20000);
            // DCFlushRange((void*)0x80000000, 0x20000);
            // ICInvalidateRange((void*)0x80000000, 0x20000);

            // custom_OSReport("Clearing range: %08x - %08x\n", (u32)(&_patches_end), 0x81800000);
            // memset((void*)&_patches_end, 0, (0x81800000 - (u32)(&_patches_end)));

            // lowmem
            *(u32*)0x8000001c = 0x00000000;
            *(u32*)0x80000020 = 0x0d15ea5e;
            *(u32*)0x80000028 = 0x01800000;
            *(u32*)0x80000024 = 0x00000001;
            *(u32*)0x8000002c = 0x00000003;

            // sys vars
            *(u32*)0x800000d0 = 0x01000000;
            *(u32*)0x800000ec = 0x81800000;
            *(u32*)0x800000f0 = 0x01800000;
            *(u32*)0x800000f4 = 0x00000000;
        }
    }

    dol_info_t info = load_dol(dol_offset, 0);
    custom_OSReport("Booting... (%08x)\n", (u32)info.entrypoint);

    char *game_code = (char*)&lowmem->b_disk_info.game_code[0];

    if (cbvmc_wanted) {
        // cubeboot_vmc: the game DOL is in RAM at its final addresses and nothing of it has run yet.  The call checks the game and
        // the word table, copies the resident image into the reserve and redirects one OSInit call to it; on any refusal nothing is
        // written and the game boots exactly as without this patch.
        struct cbvmc_loader_cfg cfg;
        cfg.ram = (uint8_t*)0x80000000;
        cfg.ram_base = 0x80000000;
        cfg.ram_size = 0x01800000;
        cfg.game_code = game_code;
        cfg.dol_max = info.max_addr;
        cfg.bi2_addr = bi2Addr;
        cfg.res_image = cbvmc_resident_bin;
        cfg.res_len = cbvmc_resident_bin_size;
        cfg.ins_image = cbvmc_install_bin;
        cfg.ins_len = cbvmc_install_bin_size;
        int vmc;
        {
            // scan the game's code sections as they sit in RAM, write the report, install what the scan found (all or nothing).  A refused scan whose
            // install point is still proven (unique `bl EXIInit` right after __OSInitSystemCall) gets the KEEP-ALIVE install: card files, blob, vector and
            // the T2 watch, no word patched, so a later executable of the same session (PAL 50/60 Hz stub -> game, menu -> game) is still caught.
            static struct cbvmc_scan_result sr;
            static char report[1024] __attribute__((aligned(32)));
            struct cbvmc_range rg[CBVMC_SCAN_MAX_RANGES];
            unsigned nrg = 0, len, k;
            for (k = 0; k < MAXTEXTSECTION && nrg < CBVMC_SCAN_MAX_RANGES; k++) {
                if (dol_hdr.textAddress[k] && dol_hdr.textLength[k]) { rg[nrg].lo = dol_hdr.textAddress[k]; rg[nrg].hi = dol_hdr.textAddress[k] + dol_hdr.textLength[k]; nrg++; }
            }
            cbvmc_scan(cfg.ram, cfg.ram_base, rg, nrg, &sr);
            len = cbvmc_scan_report(&sr, rg, nrg, game_code, report, sizeof(report) - 32);
            custom_OSReport("%s", report);
            while (len & 31) report[len++] = '\n';                           // the Flippy file API writes whole 32-byte blocks
            if (cbvmc_ipl_diag & CBVMC_IPLD_STATUS) cbvmc_write_report(report, len);     // vmc_diag = 1: status files on the SD
            if (sr.ok > 0) vmc = cbvmc_loader_install_generic(&cfg, sr.words, sr.nwords, sr.hook_addr, sr.hook_target);
            else if (sr.hook_addr && sr.hook_target && sr.syscall_init && sr.refuse != CBVMC_R_RANGE) vmc = cbvmc_loader_install_generic(&cfg, 0, 0, sr.hook_addr, sr.hook_target);
            else vmc = -99;
        }
        if (vmc == 0) cbvmc_flash_set_allow(cbvmc_allow, vmc, cbvmc_layer_of_game(game_code));    // slot mask, the layer's card paths and the geometry go into the resident image      // per-slot mask, the config.ini card paths and vmc_t2 go into the resident image the install image reads
        custom_OSReport("cbvmc loader: %d\n", vmc);
    }

    bool is_tonyhawk_pro_4 = (game_code[0] == 'G' && game_code[1] == 'T' && game_code[2] == '4');
    if (is_tonyhawk_pro_4) {
        custom_OSReport("Tony Hawk Pro Skater 4 detected\n");

        u32 GXPeekARGB_offset = 0;
        u32 GXPokeARGB_offset = 0;
        switch(game_code[3]) {
            case 'P':
                GXPeekARGB_offset = 0x8018fce0;
                GXPokeARGB_offset = 0x8018fd04;
                break;
            case 'F':
                GXPeekARGB_offset = 0x8019024c;
                GXPokeARGB_offset = 0x80190270;
                break;
            case 'D':
                GXPeekARGB_offset = 0x8019024c;
                GXPokeARGB_offset = 0x80190270;
                break;
            case 'E':
                GXPeekARGB_offset = 0x8019013c;
                GXPokeARGB_offset = 0x80190160;
                break;
            default:
                custom_OSReport("Unknown region code: %c\n", game_code[3]);
                while(1);
                break;
        }

        memcpy((void*)GXPeekARGB_offset, GXPeekARGBPatch_bin, GXPeekARGBPatch_length);
        DCFlushRange((void*)GXPeekARGB_offset, GXPeekARGBPatch_length);
        ICInvalidateRange((void*)GXPeekARGB_offset, GXPeekARGBPatch_length);

        memcpy((void*)GXPokeARGB_offset, GXPokeARGBPatch_bin, GXPokeARGBPatch_length);
        DCFlushRange((void*)GXPokeARGB_offset, GXPokeARGBPatch_length);
        ICInvalidateRange((void*)GXPokeARGB_offset, GXPokeARGBPatch_length);
    }

    // custom_OSReport("Running game...\n");
    // udelay(1 * 1000 * 1000);
    // while(1);

    run(info.entrypoint);
    __builtin_unreachable();
}

void run(register void* entry_point) {
    // ICFlashInvalidate
    asm("mfhid0	4");
    asm("ori 4, 4, 0x0800");
    asm("mthid0	4");
    // hwsync
    asm("sync");
    asm("isync");
    // boot
    asm("mtlr %0" : : "r" (entry_point));
    asm("blr");
}
