#include "bs2.h"

#include "attr.h"
#include "boot.h"
#include "dol.h"
#include "flippy_sync.h"
#include "gameid.h"
#include "games.h"
#include "gc_dvd.h"
#include "menu.h"
#include "os.h"
#include "reloc.h"
#include "time.h"

#include "default_opening_bin.h"

#include <gctypes.h>

bool is_disc_drive_selected = false;
static bool is_disc_drive_active = false;
static bool is_switching_device = false;

__attribute_data__ u32 force_swiss_boot = 0;

// used for optional delays
__attribute_data__ u32 postboot_delay_ms = 0;
__attribute_data__ u64 completed_time = 0;

// used to start game
__attribute_reloc__ u32 (*PADSync)();
__attribute_reloc__ void (*__OSStopAudioSystem)();
// __attribute_reloc__ void (*run)(register void* entry_point, register u32 clear_start, register u32 clear_size);

// Top-level menu
__attribute_reloc__ u16 *top_level_banner_element_alpha; // This could be a `element_alpha_state_t` (once that's merged), but we only need the first member

extern u16 *cube_menu_alpha;
extern u32 *banner_ready;
extern const BNR **banner_pointer;
extern u32 start_passthrough_game;

void bs2init() {
    is_disc_drive_selected = start_passthrough_game;
    is_disc_drive_active = is_disc_drive_selected;
    is_switching_device = false;

    if (is_disc_drive_active) {
        gm_start_disc_thread();
    } else {
        gm_start_thread("/");
    }
}

bool bs2_is_switching_device() {
    return is_switching_device;
}

__attribute_data__ int frame_count = 0;
u32 bs2tick_flippydrive() {
    frame_count++;
    if (!completed_time && cube_state->cube_anim_done) {
        OSReport("FINISHED (%d frames)\n", frame_count);
        completed_time = gettime();
    }

    if (start_passthrough_game) {
        if (postboot_delay_ms) {
            u64 elapsed = diff_msec(completed_time, gettime());
            if (completed_time > 0 && elapsed > postboot_delay_ms) {
                return STATE_START_GAME;
            } else {
                return STATE_WAIT_LOAD;
            }
        }
        return STATE_START_GAME;
    }

    // this helps the start menu show correctly
    if (*main_menu_id >= 3) {
        return STATE_START_GAME;
    }

#ifdef TEST_SKIP_ANIMATION
    return STATE_COVER_OPEN;
#endif

    // TODO: allow the user to decide if they want to logo to play
    return STATE_NO_DISC;
}

u32 bs2tick_disc() {
    // If the disc thread is running, do things relating to it
    *banner_ready = disc_read_banner_ready;
    *banner_pointer = stock_banner_ptr;
    return disc_read_state;
}

void bs2tick_check_device_switch() {
    if ((is_disc_drive_selected != is_disc_drive_active) && !is_switching_device) {
        // Begin switching to the new device
        is_switching_device = true;

        if (is_disc_drive_active) {
            // Request the disc drive thread to stop
            request_disc_stop_thread = true;

        } else {
            // TODO: Can we do the same for the FlippyDrive thread?
        }
    }

    if (is_switching_device) {
        // Before completing the switch, make sure the banner on the menu's finished fading out
        // Note that the banner alpha is only updated while on the top-level menu, so check if the top-level menu's visible too
        // (The banner alpha also doesn't change during the startup animation, and is instead always set to 0)
        bool is_banner_visible = *top_level_banner_element_alpha > 0 && *cube_menu_alpha < 0x7FFF;

        if (!is_banner_visible) {
            // If the thread's stopped, restart it and stop switching
            if (is_disc_drive_active) {
                if (!game_disc_running) {
                    gm_deinit_thread();
                    is_switching_device = false;
                }
            } else {
                if (!game_enum_running) {
                    gm_deinit_thread();
                    is_switching_device = false;
                }
            }
        }

        if (!is_switching_device) {
            // Start spinning up the new thread
            is_disc_drive_active = is_disc_drive_selected;
            if (is_disc_drive_active) {
                gm_start_disc_thread();
            } else {
                if (*cur_menu_id != MENU_GAMESELECT_TRANSITION_ID) {
                    *banner_pointer = (const BNR *)&default_opening_bin[0];
                    *banner_ready = 1;
                }
                gm_start_thread("/");
            }
        }
    }
}

__attribute_used__ u32 bs2tick() {
    // TODO: On boot, try each device in a configurable order, and stick with the first successful one
    bs2tick_check_device_switch();
    if (is_switching_device) {
        // Just show as 'loading' while we wait
        return STATE_WAIT_LOAD;
    }

    if (is_disc_drive_active) {
        return bs2tick_disc();
    } else {
        return bs2tick_flippydrive();
    }
}

__attribute_used__ void bs2start() {
    OSReport("DONE\n");

    // read boot info into lowmem
    struct dolphin_lowmem *lowmem = (struct dolphin_lowmem*)0x80000000;

    if (!is_disc_drive_active) {
        gm_deinit_thread();
    } else {
        request_disc_start_game = true;
        gm_deinit_thread();

        int ret = dvd_read_id();
        int err = dvd_get_error();
        if (ret != 0 || err != 0) {
            custom_OSReport("Failed to read disc ID\n");
            dvd_custom_bypass_exit();
            udelay(10 * 1000);

            load_stub(); // exit to loader again
            u32 *sig = (u32*)0x80001804;
            if ((*sig++ == 0x53545542 || *sig++ == 0x53545542) && *sig == 0x48415858) {
                static void (*reload)(void) = (void(*)(void))0x80001800;
                run(reload);
            }
        }

        custom_OSReport("Game ID: %c%c%c%c\n", lowmem->b_disk_info.game_code[0], lowmem->b_disk_info.game_code[1], lowmem->b_disk_info.game_code[2], lowmem->b_disk_info.game_code[3]);
        dvd_audio_config(lowmem->b_disk_info.audio_streaming, lowmem->b_disk_info.stream_buffer_size);

        char diskName[64] = "DISC GAME\0";
        setup_gameid_commands(&lowmem->b_disk_info, diskName);
    }

    // no IPL code should be running after this point

    while (!PADSync());
    OSDisableInterrupts();
    __OSStopAudioSystem();

    u32 start_addr = 0x80100000;
    u32 end_addr = 0x81600000;
    u32 len = end_addr - start_addr;

    memset((void*)start_addr, 0, len); // cleanup
    DCFlushRange((void*)start_addr, len);
    ICInvalidateRange((void*)start_addr, len);

    // Passthrough mode
    if (is_disc_drive_active) {
        chainload_boot_game(NULL, true);
    }

    char *boot_path = boot_entry.path;
    if (boot_entry.type == GM_FILE_TYPE_PROGRAM) {
        custom_OSReport("Booting DOL\n");
        load_stub();

        dol_info_t info = load_dol_file(boot_path, false);
        run(info.entrypoint);
    } else {
        custom_OSReport("Booting ISO\n");

        if (!force_swiss_boot) {
            custom_OSReport("Booting ISO (custom apploader)\n");
            chainload_boot_game(&boot_entry, false);
        } else {
            custom_OSReport("Booting ISO (swiss chainload)\n");
            chainload_swiss_game(boot_path, false);
        }
    }

    __builtin_unreachable();
}
