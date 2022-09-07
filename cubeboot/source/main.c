#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <ogcsys.h>
#include <gccore.h>
#include <unistd.h>

#include <asndlib.h>
#include <ogc/lwp_threads.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <sdcard/gcsd.h>
#include "ffshim.h"
#include "fatfs/ff.h"
#include "utils.h"
#include "crc32.h"
#include "dol.h"

#include "ipl.h"
#include "patches_elf.h"
#include "elf.h"

#include "print.h"
#include "helpers.h"

#define VIDEO_ENABLE
#define CONSOLE_ENABLE
#define PRINT_PATCHES

u8 *prog_buf = (u8*)(PROG_ADDR);

void dol_alloc(int size);
u8 *dol_buf = NULL;

extern DOLHEADER *dolhdr;
extern u32 minaddress;
extern u32 maxaddress;
extern u32 _entrypoint, _dst, _src, _len;

#define BS2_BASE_ADDR 0x81300000
static void (*bs2entry)(void) = (void(*)(void))BS2_BASE_ADDR;
// static void (*stubentry)(void) = (void(*)(void))PROG_ADDR;

static char stringBuffer[0x80];

// // text logo replacment
// void *gc_text_tex_data_ptr;
// extern void render_logo();

GXRModeObj *rmode;
void *xfb;

int load_fat_swiss(const char *slot_name, const DISC_INTERFACE *iface_);

char *swiss_paths[] = {
    "/BOOT.DOL",
    "/BOOT2.DOL",
    "/IGR.DOL", // used by swiss-gc
    "/IPL.DOL", // used by iplboot
    "/AUTOEXEC.DOL", // used by ActionReplay
};

void __SYS_PreInit() {
    SYS_SetArenaHi((void*)0x81300000);
}

int main() {
#ifdef VIDEO_ENABLE
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
#ifdef CONSOLE_ENABLE
	console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
#endif
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
    // // debug above
#endif

#ifdef DOLPHIN
    InitializeUART();
#else
    // enable printf
    CON_EnableGecko(1, true);
#endif

    // load ipl
    load_ipl();

    // load program
    if (load_fat_swiss("sdb", &__io_gcsdb)) goto load;
    if (load_fat_swiss("sda", &__io_gcsda)) goto load;
    if (load_fat_swiss("sd2", &__io_gcsd2)) goto load;

load:
    iprintf("Program loaded... [%08x]\n", *(u32*)prog_buf);

    iprintf("maxaddress = 0x%08x\n", maxaddress);
    iprintf("minaddress = 0x%08x\n", minaddress);

    Elf32_Ehdr* ehdr;
    Elf32_Shdr* shdr;
    unsigned char* image;

    void *addr = (void*)patches_elf;
    ehdr = (Elf32_Ehdr *)addr;

    // get section string table
    Elf32_Shdr* shstr = &((Elf32_Shdr *)(addr + ehdr->e_shoff))[ehdr->e_shstrndx];
    char* stringdata = (char*)(addr + shstr->sh_offset);

    // get symbol string table
    Elf32_Shdr* symshdr = SHN_UNDEF;
    char* symstringdata = SHN_UNDEF;
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff + (i * sizeof(Elf32_Shdr)));
        if (shdr->sh_type == SHT_SYMTAB) {
            symshdr = shdr;
        }
        if (shdr->sh_type == SHT_STRTAB && strcmp(stringdata + shdr->sh_name, ".strtab") == 0) {
            symstringdata = (char*)(addr + shdr->sh_offset);
        }
    }

    // get symbols
    Elf32_Sym* syment = (Elf32_Sym*) (addr + symshdr->sh_offset);

    // setup local vars
    char *patch_prefix = ".patch.";
    uint32_t patch_prefix_len = strlen(patch_prefix);
    char patch_region_suffix[128];
    sprintf(patch_region_suffix, "%s_func", current_bios->patch_suffix);

    char *reloc_prefix = ".reloc";
    u32 reloc_start = 0;
    u32 reloc_end = 0;
    char *reloc_region = current_bios->reloc_prefix;

    // Patch each appropriate section
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff + (i * sizeof(Elf32_Shdr)));

        char *sh_name = stringdata + shdr->sh_name;
        if ((!(shdr->sh_flags & SHF_ALLOC) && strncmp(patch_prefix, sh_name, patch_prefix_len) != 0) || shdr->sh_addr == 0 || shdr->sh_size == 0) {
            iprintf("Skipping ALLOC %s!!\n", stringdata + shdr->sh_name);
            continue;
        }

        shdr->sh_addr &= 0x3FFFFFFF;
        shdr->sh_addr |= 0x80000000;
        // shdr->sh_size &= 0xfffffffc;

        if (shdr->sh_type == SHT_NOBITS && strncmp(patch_prefix, stringdata + shdr->sh_name, patch_prefix_len) != 0) {
            iprintf("Skipping NOBITS %s @ %08x!!\n", stringdata + shdr->sh_name, shdr->sh_addr);
            memset((void*)shdr->sh_addr, 0, shdr->sh_size);
        } else {
            // check if this is a patch section
            uint32_t sh_size = 0;
            if (strncmp(patch_prefix, sh_name, patch_prefix_len) == 0) {
                // check if this patch is for the current IPL
                if (!ensdwith(sh_name, patch_region_suffix)) {
                    // iprintf("SKIP PATCH %s != %s\n", sh_name, patch_region_suffix);
                    continue;
                }

                // create symbol name for section size
                uint32_t sh_name_len = strlen(sh_name);
                (sh_name + patch_prefix_len)[sh_name_len - patch_prefix_len - 5] = '\x00';
                sprintf(&stringBuffer[0], "%s_size", sh_name + patch_prefix_len);

                // find symbol by name
                for (int i = 0; i < (symshdr->sh_size / sizeof(Elf32_Sym)); ++i) {
                    if (syment[i].st_name == SHN_UNDEF) {
                        continue;
                    }

                    char *symname = symstringdata + syment[i].st_name;
                    if (strcmp(symname, stringBuffer) == 0) {
                        sh_size = syment[i].st_value;
                    }
                }
            } else if (strcmp(reloc_prefix, sh_name) == 0) {
                reloc_start = shdr->sh_addr;
                reloc_end = shdr->sh_addr + shdr->sh_size;
            }

            // set section size from header if it is not provided as a symbol
            if (sh_size == 0) sh_size = shdr->sh_size;

            image = (unsigned char*)addr + shdr->sh_offset;
#ifdef PRINT_PATCHES
            iprintf("patching ptr=%x size=%04x orig=%08x val=%08x [%s]\n", shdr->sh_addr, sh_size, *(u32*)shdr->sh_addr, *(u32*)image, sh_name);
#endif
            memcpy((void*)shdr->sh_addr, (const void*)image, sh_size);
        }
    }

    // Copy symbol relocations by region
    iprintf(".reloc section [0x%08x - 0x%08x]\n", reloc_start, reloc_end);
    for (int i = 0; i < (symshdr->sh_size / sizeof(Elf32_Sym)); ++i) {
        if (syment[i].st_name == SHN_UNDEF) {
            continue;
        }

        char *current_symname = symstringdata + syment[i].st_name;
        if (syment[i].st_value >= reloc_start && syment[i].st_value < reloc_end) {
            sprintf(stringBuffer, "%s_%s", reloc_region, current_symname);
            // iprintf("reloc: Looking for symbol named %s\n", stringBuffer);
            u32 val = get_symbol_value(symshdr, syment, symstringdata, stringBuffer);
            
            if (val != 0) {
                iprintf("Found reloc %s = %x, val = %08x\n", current_symname, syment[i].st_value, val);
                *(u32*)syment[i].st_value = val;
            } else {
                iprintf("ERROR broken reloc %s = %x\n", current_symname, syment[i].st_value);
            }
        }
    }

    // Copy program metadata into place
    set_patch_value(symshdr, syment, symstringdata, "prog_entrypoint", _entrypoint);
    set_patch_value(symshdr, syment, symstringdata, "prog_dst", _dst);
    set_patch_value(symshdr, syment, symstringdata, "prog_src", _src);
    set_patch_value(symshdr, syment, symstringdata, "prog_len", _len);

    // while(1);

#ifdef CONSOLE_ENABLE
    VIDEO_WaitVSync();
#endif

    /*** Shutdown libOGC ***/
    GX_AbortFrame();
    ASND_End();
    u32 bi2Addr = *(volatile u32*)0x800000F4;
    u32 osctxphys = *(volatile u32*)0x800000C0;
    u32 osctxvirt = *(volatile u32*)0x800000D4;
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    *(volatile u32*)0x800000F4 = bi2Addr;
    *(volatile u32*)0x800000C0 = osctxphys;
    *(volatile u32*)0x800000D4 = osctxvirt;
    /*** Shutdown all threads and exit to this method ***/
    __lwp_thread_stopmultitasking(bs2entry);

    __builtin_unreachable();
}

int load_fat_swiss(const char *slot_name, const DISC_INTERFACE *iface_) {
    int res = 0;

    iprintf("Trying %s\n", slot_name);

    FATFS fs;
    iface = iface_;
    FRESULT mount_result = f_mount(&fs, "", 1);
    if (mount_result != FR_OK) {
        iprintf("Couldn't mount %s: %s\n", slot_name, get_fresult_message(mount_result));
        goto end;
    }

    char name[256];
    f_getlabel(slot_name, name, NULL);
    iprintf("Mounted %s as %s\n", name, slot_name);

    for (int f = 0; f < (sizeof(swiss_paths) / sizeof(char *)); f++) {
        char *path = swiss_paths[f];

        iprintf("Reading %s\n", path);
        FIL file;
        FRESULT open_result = f_open(&file, path, FA_READ);
        if (open_result != FR_OK)
        {
            iprintf("Failed to open file: %s\n", get_fresult_message(open_result));
            continue;
        }

        size_t size = f_size(&file);
        dol_alloc(size);
        if (!dol_buf) {
            iprintf("Failed to allocate memory\n");
            goto unmount;
        }

        u32 unused;
        f_read(&file, dol_buf, size, &unused);
        f_close(&file);

        iprintf("Loaded DOL into %p\n", dol_buf);

        DOLtoMRAM(dol_buf);

        res = 1;
        break;
    }

unmount:
    iprintf("Unmounting %s\n", slot_name);
    iface->shutdown();
    iface = NULL;

end:
    return res;
}

void dol_alloc(int size) {
    int mram_size = (SYS_GetArenaHi() - SYS_GetArenaLo());
    iprintf("Memory available: %iB\n", mram_size);

    iprintf("DOL size is %iB\n", size);

    if (size <= 0) {
        iprintf("Empty DOL\n");
        return;
    }

    dol_buf = (u8*)memalign(32, size);

    if (!dol_buf) {
        iprintf("Couldn't allocate memory\n");
    }
}
