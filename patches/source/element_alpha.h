#pragma once

#include <gctypes.h>

typedef struct {
    u16 current_alpha; // Ranges from 0 to (fade_duration + start_delay)
    u16 fade_duration; // Frames; typically 0x14
    u16 start_delay; // Frames; typically 0
    u16 frame_counter; // Incremented with every update
    u8 unk0;
    u8 max_output;
    u16 unknown_output_multiplier;
    u32 unk1;
} element_alpha_state_t;

typedef enum {
    element_alpha_visible,
    element_alpha_hidden,
    element_alpha_dimmed
} element_alpha_update_state_t;

extern void (*update_element_alpha)(element_alpha_state_t *element, element_alpha_update_state_t new_state);
