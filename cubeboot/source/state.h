#include <gctypes.h>
#include "const.h"

#define MAX_BUTTONS 13

#define BUTTON_UP 0
#define BUTTON_DOWN 1

typedef struct cubeboot_state_t {
    u32 boot_code;
    u16 padding;
    u16 last_buttons;
    struct {
        u32 status;
        u64 timestamp;
    } held_buttons[MAX_BUTTONS];
} cubeboot_state;

extern cubeboot_state *state;
