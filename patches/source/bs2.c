#include "bs2.h"

#include "attr.h"
#include "device_selector.h"
#include "boot.h"
#include "dol.h"
#include "element_alpha.h"
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
#include <stdlib.h>

#define MAIN_MENU_ID_SETUP_ERROR 0
#define MAIN_MENU_ID_READING_DISC_ANIMATION 1
#define MAIN_MENU_ID_RTC_ERROR 2
#define MAIN_MENU_ID_NO_DISC_FADE_IN 3
#define MAIN_MENU_ID_ANIMATING_TO_MENU 4
#define MAIN_MENU_ID_MENU_ACTIVE 5

typedef enum {
    device_waiting,
    device_ready,
    device_not_ready
} device_state_t;

device_t selected_device;
static device_t active_device;
static bool is_switching_device = false;

bool finished_automatic_switching = false;
__attribute_data__ device_t *boot_devices;
__attribute_data__ u32 boot_devices_count;
static u32 current_boot_device_index = 0;
static device_state_t current_device_state = device_waiting;

__attribute_data__ u32 force_swiss_boot = 0;

// used for optional delays
__attribute_data__ u32 postboot_delay_ms = 0;
__attribute_data__ u64 completed_time = 0;

__attribute_data__ u32 is_disc_drive_allowed = 1;

static bool has_bs2init_run = false;

// used to start game
__attribute_reloc__ u32 (*PADSync)();
__attribute_reloc__ void (*__OSStopAudioSystem)();
// __attribute_reloc__ void (*run)(register void* entry_point, register u32 clear_start, register u32 clear_size);

// Top-level menu
__attribute_reloc__ element_alpha_state_t *top_level_banner_element_alpha;
__attribute_reloc__ u16 *cube_menu_alpha;

extern u32 *banner_ready;
extern const BNR **banner_pointer;
extern u32 start_passthrough_game;

bool bs2_is_device_allowed(device_t device) {
    switch (device) {
        case device_disc_drive:
            return is_disc_drive_allowed;

        default:
            return true;
    }
}

u32 bs2_get_next_allowed_device_index(u32 start_index) {
    for (u32 i = start_index; i < boot_devices_count; i++) {
        if (bs2_is_device_allowed(boot_devices[i])) {
            return i;
        }
    }

    // No more compatible devices
    return boot_devices_count;
}

void bs2init() {
    set_device_selector_enabled(is_disc_drive_allowed);

    current_boot_device_index = bs2_get_next_allowed_device_index(0);

    if (current_boot_device_index < boot_devices_count) {
        selected_device = boot_devices[current_boot_device_index];
        finished_automatic_switching = false;
    } else {
        selected_device = start_passthrough_game && is_disc_drive_allowed ? device_disc_drive : device_flippydrive;
        finished_automatic_switching = true;
    }

    active_device = selected_device;
    is_switching_device = false;
    current_device_state = device_waiting;

    switch (active_device) {
        case device_disc_drive:
            gm_start_disc_thread();
            break;

        case device_flippydrive:
            gm_start_thread("/");
            break;
    }

    has_bs2init_run = true;
}

bool bs2_is_switching_device() {
    return is_switching_device;
}

u32 bs2tick_flippydrive() {
    // For now, assume that the FlippyDrive is ready to go
    // Ideally, this should check if the network or SD card is accessible
    current_device_state = device_ready;

    // this helps the start menu show correctly
    if (*main_menu_id >= MAIN_MENU_ID_NO_DISC_FADE_IN) {
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
    u32 found_disc_read_state = disc_read_state;

    if (found_disc_read_state == STATE_START_GAME) {
        current_device_state = device_ready;
    } else if (found_disc_read_state == STATE_NO_DISC || found_disc_read_state == STATE_COVER_OPEN || found_disc_read_state == STATE_READ_ERROR || found_disc_read_state == STATE_FATAL_ERROR) {
        current_device_state = device_not_ready;
    } else {
        current_device_state = device_waiting;
    }

    // Handle the post-boot delay
    if (found_disc_read_state == STATE_START_GAME && postboot_delay_ms) {
        u64 elapsed = diff_msec(completed_time, gettime());
        if (completed_time == 0 || elapsed < postboot_delay_ms) {
            return STATE_WAIT_LOAD;
        }
    }

    return found_disc_read_state;
}

void bs2tick_check_device_switch() {
    if ((selected_device != active_device) && !is_switching_device) {
        // Begin switching to the new device
        is_switching_device = true;
        current_device_state = device_waiting;

        if (active_device == device_disc_drive) {
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
        bool is_banner_visible = top_level_banner_element_alpha->current_alpha > 0 && *cube_menu_alpha < 0x7FFF;

        if (!is_banner_visible) {
            // If the thread's stopped, restart it and stop switching
            switch (active_device) {
                case device_disc_drive:
                    if (!game_disc_running) {
                        gm_deinit_thread();
                        is_switching_device = false;
                    }
                    break;

                case device_flippydrive:
                    if (!game_enum_running) {
                        gm_deinit_thread();
                        is_switching_device = false;
                    }
                    break;
            }
        }

        if (!is_switching_device) {
            // Start spinning up the new thread
            active_device = selected_device;
            switch (active_device) {
                case device_disc_drive:
                    gm_start_disc_thread();
                    break;

                case device_flippydrive:
                    if (*cur_menu_id != MENU_GAMESELECT_TRANSITION_ID) {
                        *banner_pointer = (const BNR *)&default_opening_bin[0];
                        *banner_ready = 1;
                    }
                    gm_start_thread("/");
                    break;
            }
        }
    }
}

void bs2tick_auto_device_switch() {
    if (finished_automatic_switching) {
        // Automatic switching isn't active
        return;
    }

    if (*main_menu_id >= MAIN_MENU_ID_ANIMATING_TO_MENU) {
        // The GameCube logo is finished and we're transitioning to the main menu;
        // disable automatic switching, handing control to the user
        finished_automatic_switching = true;
        return;
    }

    switch (current_device_state) {
    case device_waiting:
        // Keep waiting for the device to finish loading
        break;

    case device_ready:
        // The current device is ready to go - stop auto-switching
        finished_automatic_switching = true;
        break;

    case device_not_ready:
        // The current device isn't currently usable (e.g. no disc); switch to the next one
        u32 new_boot_device_index = bs2_get_next_allowed_device_index(current_boot_device_index + 1);

        if (new_boot_device_index < boot_devices_count) {
            current_boot_device_index = new_boot_device_index;
            selected_device = boot_devices[current_boot_device_index];
        } else {
            // We've tried every device, and none are ready; just stick with the last one
            finished_automatic_switching = true;
        }
        break;
    }
}

__attribute_data__ int frame_count = 0;
__attribute_used__ u32 bs2tick() {
    if (!has_bs2init_run) {
        return STATE_WAIT_LOAD;
    }

    frame_count++;
    if (!completed_time && cube_state->cube_anim_done) {
        OSReport("FINISHED (%d frames)\n", frame_count);
        completed_time = gettime();
    }

    while (true) {
        bs2tick_check_device_switch();
        if (is_switching_device) {
            // Just show as 'loading' while we wait
            return STATE_WAIT_LOAD;
        }

        u32 return_value = STATE_FATAL_ERROR;
        switch (active_device) {
            case device_disc_drive:
                return_value = bs2tick_disc();
                break;

            case device_flippydrive:
                return_value = bs2tick_flippydrive();
                break;
        }

        // Given the return value, check if we need to automatically switch to another device
        bs2tick_auto_device_switch();
        if (selected_device != active_device) {
            continue;
        }

        return return_value;
    }
}

__attribute_used__ void bs2start() {
    OSReport("DONE\n");

    // read boot info into lowmem
    struct dolphin_lowmem *lowmem = (struct dolphin_lowmem*)0x80000000;

    if (active_device != device_disc_drive) {
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
    if (active_device == device_disc_drive) {
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
