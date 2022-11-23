// dol loading

#include <malloc.h>

#include "sd.h"

#include "crc32.h"
#include "print.h"
#include "halt.h"

#include "loader.h"
#include "boot/sidestep.h"

char *boot_paths[] = {
    "/BOOT.DOL",
    "/BOOT2.DOL",
    "/IGR.DOL", // used by swiss-gc
    "/IPL.DOL", // used by iplboot
    "/AUTOEXEC.DOL", // used by ActionReplay
};

extern const void _start;
extern const void _edata;

u8 *dol_buf;

bool check_load_program() {
    // check if we can even load files
    if (!is_device_mounted()) return false;

    bool found_file = false;
    for (int f = 0; f < (sizeof(boot_paths) / sizeof(char *)); f++) {
        char *path = boot_paths[f];
        int size = get_file_size(path);
        if (size == SD_FAIL) {
            iprintf("Failed to open file: %s\n", path);
            continue;
        } else {
            found_file = true;
            break;
        }
    }

    return found_file;
}

bool load_cli_file(char *path, cli_params *params) {
    char cli_path[255];
    strcpy(cli_path, path);

    int path_length = strlen(cli_path);
    cli_path[path_length - 3] = 'c';
    cli_path[path_length - 2] = 'l';
    cli_path[path_length - 1] = 'i';

    // always set program name
    params->argv[params->argc] = path;
    params->argc++;

    iprintf("Reading CLI %s\n", cli_path);

    int size = get_file_size(cli_path);
    if (size == SD_FAIL) {
        iprintf("Failed to open file: %s\n", cli_path);
        return false;
    }

    char *cli_buf;
    if (load_file_dynamic(cli_path, (void*)&cli_buf) != SD_OK) {
        iprintf("Failed to CLI read file: %s\n", cli_path);
        return false;
    }

    // First argument is at the beginning of the file
    if (cli_buf[0] != '\r' && cli_buf[0] != '\n') {
        params->argv[params->argc] = cli_buf;
        params->argc++;
    }

    // Search for the others after each newline
    for (int i = 0; i < size; i++) {
        if (cli_buf[i] == '\r' || cli_buf[i] == '\n')
        {
            cli_buf[i] = '\0';
        }
        else if (cli_buf[i - 1] == '\0')
        {
            params->argv[params->argc] = cli_buf + i;
            params->argc++;
            if (params->argc >= MAX_NUM_ARGV)
            {
                kprintf("Reached max of %i args.\n", MAX_NUM_ARGV);
                break;
            }
        }
    }

    iprintf("Found %i CLI args\n", params->argc);

    return true;
}

bool load_program(char *path, cli_params *params) {
    iprintf("Reading %s\n", path);

    int size = get_file_size(path);
    if (size == SD_FAIL) {
        iprintf("Failed to open file: %s\n", path);
        return false;
    }

    dol_buf = memalign(32, size);
    if (!dol_buf) {
        dol_buf = (u8*)0x81300000;
    }

    if (load_file_buffer(path, dol_buf) != SD_OK) {
        iprintf("Failed to DOL read file: %s\n", path);
        dol_buf = NULL;
        return false;
    }

    iprintf("Loaded DOL into %p\n", dol_buf);

    // try to get cli args
    load_cli_file(path, params);

    return true;
}

void boot_program(char *alternative_path) {
    cli_params params = { .argc = 0 };
    memset(params.argv, 0, sizeof(char*));

    if (alternative_path != NULL) {
        load_program(alternative_path, &params);
    } else {
        for (int f = 0; f < (sizeof(boot_paths) / sizeof(char *)); f++) {
            char *path = boot_paths[f];
            if (load_program(path, &params)) {
                break;
            }
        }
    }

    if (dol_buf == NULL) {
        prog_halt("No program loaded!\n");
        return;
    }

    iprintf("Program loaded...\n");

    iprintf("BOOTING\n");
#ifdef VIDEO_ENABLE
    VIDEO_WaitVSync();
#endif

    DOLtoARAM(dol_buf, params.argc, params.argv);
}
