#include <gctypes.h>

typedef struct {
    u16 current_alpha; // Typically 0x00-0x14
    u16 unk0;
    u16 unk1;
    u16 unk2;
    u16 unk3;
    u16 unk4;
    u16 unk5;
    u16 unk6;
} element_alpha_state_t;

typedef struct {
    struct {
        struct {
            element_alpha_state_t left_control_stick;
            element_alpha_state_t centre_b_button;
            element_alpha_state_t right_a_button;
        } three_columns;

        struct {
            element_alpha_state_t control_stick;
        } one_column;

        struct {
            element_alpha_state_t left_b_button;
            element_alpha_state_t right_a_button;
        } two_columns;
    } icons;

    struct {
        struct {
            element_alpha_state_t menu_selection;
        } one_column;

        struct {
            element_alpha_state_t left_cancel;
            element_alpha_state_t right_confirm;
        } two_columns;

        struct {
            element_alpha_state_t left_select;
            element_alpha_state_t left_change;
            element_alpha_state_t centre_cancel;
            element_alpha_state_t centre_finish;
            element_alpha_state_t right_confirm;
        } three_columns;
    } text;

    // There are more elements past this point that relate to other elements, e.g. menus
} all_element_alphas_t;
