#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <ogcsys.h>
#include <gccore.h>
#include <unistd.h>

#include <asndlib.h>
#include <ogc/lwp_threads.h>
#include <ogc/lwp_watchdog.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>

#include "crc32.h"

#include "ipl.h"
#include "patches_elf.h"
#include "elf.h"

#include "boot/sidestep.h"
#include "sd.h"

#include "print.h"
#include "helpers.h"
#include "halt.h"

#include "state.h"
#include "settings.h"

#include "config.h"
#include "gcm.h"
#include "bnr.h"

#include "flippy_sync.h"
#include "sram.h"

#define DEFAULT_FIFO_SIZE (256 * 1024)

#define STUB_ADDR 0x80001800

#define BS2_BASE_ADDR 0x81300000
static void (*bs2entry)(void) = (void(*)(void))BS2_BASE_ADDR;

static char stringBuffer[255];
ATTRIBUTE_ALIGN(32) u8 current_dol_buf[750 * 1024];
u32 current_dol_len;

extern const void _start;
extern const void _edata;
extern const void _end;

void *xfb;
GXRModeObj *rmode;

// this will be used during system init
void *__attribute__((used)) __myArena1Hi = (void*)BS2_BASE_ADDR;

int main(int argc, char **argv) {
    u64 startts, endts;

    startts = ticks_to_millisecs(gettime());


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

#ifdef VIDEO_ENABLE
	VIDEO_Init();
    // rmode = VIDEO_GetPreferredMode(NULL);
    // rmode = &TVNtsc480IntDf;
    u32 tvmode = VIDEO_GetCurrentTvMode();
    switch (tvmode) {
        case VI_NTSC:
            rmode = &TVNtsc480IntDf;
            break;
        case VI_PAL:
            rmode = &TVPal528IntDf; // libogc uses TVPal576IntDfScale
            break;
        case VI_MPAL:
            rmode = &TVMpal480IntDf;
            break;
        case VI_EURGB60:
            rmode = &TVEurgb60Hz480IntDf;
            break;
    }
    xfb = MEM_K0_TO_K1(ROUND_UP_1K(get_symbol_value(symshdr, syment, symstringdata, "_patches_end"))); // above patches, below stack
#ifdef CONSOLE_ENABLE
	console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
#endif
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }
    else {
        while (VIDEO_GetNextField())
            VIDEO_WaitVSync();
    }

#endif

#ifdef DOLPHIN_PRINT_ENABLE
    InitializeUART();
#endif

#ifdef GECKO_PRINT_ENABLE
    // enable printf
    CON_EnableGecko(EXI_CHANNEL_1, FALSE);
#endif

#ifdef VIDEO_ENABLE
    iprintf("XFB = %08x [max=%x]\n", (u32)xfb, VIDEO_GetFrameBufferSize(rmode));
    // free(backing_framebuffer); // free the backing framebuffer, we don't need it anymore
#endif

    ipl_metadata_t *metadata = (void*)0x81500000 - sizeof(ipl_metadata_t);
    iprintf("EARLY Metadata:\n");
    iprintf("\tMagic: %x\n", metadata->magic);
    iprintf("\tRevision: %x\n", metadata->revision);
    iprintf("\tBlob checksum: %x\n", metadata->blob_checksum);
    iprintf("\tCode size: %x\n", metadata->code_size);
    iprintf("\tCode checksum: %x\n", metadata->code_checksum);

    // setup passthrough arg
    u32 force_passthrough = 0;
    if (argc > 1 && strcmp(argv[1], "passthrough") == 0) {
        force_passthrough = 1;
    }

    if (*(u32*)STUB_ADDR == 0x50415353) {
        force_passthrough = 1;
    }

    iprintf("force_passthrough = %d\n", force_passthrough);

    // setup settings
    iprintf("Loading settings\n");
    load_settings();

    // fix sram
    set_sram_swiss(true);
    create_swiss_config();

//// fun stuff

    // load ipl
    bool is_running_dolphin = is_dolphin();
    load_ipl(is_running_dolphin);

    // disable progressive on unsupported IPLs
    if (current_bios->version == IPL_NTSC_10) {
        settings.progressive_enabled = FALSE;
    }

//// elf world pt2

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

        const char *sh_name = stringdata + shdr->sh_name;
        if ((!(shdr->sh_flags & SHF_ALLOC) && strncmp(patch_prefix, sh_name, patch_prefix_len) != 0) || shdr->sh_addr == 0 || shdr->sh_size == 0) {
            iprintf("Skipping ALLOC %s!!\n", stringdata + shdr->sh_name);
            continue;
        }

        shdr->sh_addr &= 0x3FFFFFFF;
        shdr->sh_addr |= 0x80000000;
        // shdr->sh_size &= 0xfffffffc;

        if (shdr->sh_type == SHT_NOBITS && strncmp(patch_prefix, stringdata + shdr->sh_name, patch_prefix_len) != 0) {
            iprintf("Skipping NOBITS %s @ %08x!!\n", stringdata + shdr->sh_name, shdr->sh_addr);
            // TODO: this area may overlap heap (like with .data_lowmem)
            // if (shdr->sh_addr > (u32)&_end) memset((void*)shdr->sh_addr, 0, shdr->sh_size);
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
 
                strcpy(stringBuffer, sh_name);
                stringBuffer[sh_name_len - 5] = '\x00';
                strcat(&stringBuffer[0], "_size");
                char* current_symname = stringBuffer + patch_prefix_len;

                // find symbol by name
                for (int i = 0; i < (symshdr->sh_size / sizeof(Elf32_Sym)); ++i) {
                    if (syment[i].st_name == SHN_UNDEF) {
                        continue;
                    }

                    char *symname = symstringdata + syment[i].st_name;
                    if (strcmp(symname, current_symname) == 0) {
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

    bool failed_patching = false;

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
            
            // if (strcmp(current_symname, "OSReport") == 0) {
            //     iprintf("OVERRIDE OSReport with iprintf\n");
            //     val = (u32)&iprintf;
            // }

            if (val != 0) {
#ifdef PRINT_RELOCS
                iprintf("Found reloc %s = %x, val = %08x\n", current_symname, syment[i].st_value, val);
#endif
                *(u32*)syment[i].st_value = val;
            } else {
                iprintf("ERROR broken reloc %s = %x\n", current_symname, syment[i].st_value);
                failed_patching = true;
            }
        }
    }

    if (failed_patching) {
        prog_halt("Failed BIOS Patching relocation\n");
    }

    // Copy settings into place
    set_patch_value(symshdr, syment, symstringdata, "start_passthrough_game", force_passthrough);
    set_patch_value(symshdr, syment, symstringdata, "cube_color", settings.cube_color);
    set_patch_value(symshdr, syment, symstringdata, "force_progressive", settings.progressive_enabled);
    set_patch_value(symshdr, syment, symstringdata, "force_swiss_boot", settings.force_swiss_default);

    set_patch_value(symshdr, syment, symstringdata, "disable_mcp_select", settings.disable_mcp_select);
    set_patch_value(symshdr, syment, symstringdata, "show_watermark", settings.show_watermark);

    set_patch_value(symshdr, syment, symstringdata, "preboot_delay_ms", settings.preboot_delay_ms);
    set_patch_value(symshdr, syment, symstringdata, "postboot_delay_ms", settings.postboot_delay_ms);

    // // Copy settings string
    // void *cube_logo_ptr = (void*)get_symbol_value(symshdr, syment, symstringdata, "cube_logo_path");
    // if (cube_logo_ptr != NULL && settings.cube_logo != NULL) {
    //     iprintf("Copying cube_logo_path: %p\n", cube_logo_ptr);
    //     strcpy(cube_logo_ptr, settings.cube_logo);
    // }

    // Copy other variables
    set_patch_value(symshdr, syment, symstringdata, "is_running_dolphin", is_running_dolphin);

    // unmount_current_device();

#ifdef VIDEO_ENABLE
    VIDEO_WaitVSync();
#endif

    iprintf("Patches applied\n");

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
    iprintf("IPL BOOTING\n");

    iprintf("DONE\n");

    endts = ticks_to_millisecs(gettime());

    u64 runtime = endts - startts;
    iprintf("Runtime = %llu\n", runtime);

    __lwp_thread_stopmultitasking(bs2entry);

    __builtin_unreachable();
}
