#include "button_descriptions.h"

#include "menu.h"
#include "attr.h"
#include "reloc.h"

#define NUM_PAL_LANGUAGES 6

__attribute_reloc__ void (*update_button_alphas)();

__attribute_reloc__ all_element_alphas_t* all_element_alphas;

__attribute_reloc__ u16 *current_pal_buttons_language;
__attribute_reloc__ element_alpha_state_t *pal_button_language_elements;

static void update_gameplay_button_text() {
    // Disable button descriptions used by the outer menu
    update_element_alpha(&all_element_alphas->text.one_column.menu_selection, element_alpha_hidden);
    update_element_alpha(&all_element_alphas->text.two_columns.left_cancel, element_alpha_hidden);
    update_element_alpha(&all_element_alphas->text.two_columns.right_confirm, element_alpha_hidden);

    // And also disable button descriptions used by other menus
    update_element_alpha(&all_element_alphas->text.three_columns.left_change, element_alpha_hidden);
    update_element_alpha(&all_element_alphas->text.three_columns.centre_finish, element_alpha_hidden);

    // Show 'B: Cancel' on both the loader and start screens
    update_element_alpha(&all_element_alphas->text.three_columns.centre_cancel, element_alpha_visible);

    // Show 'Control Stick: Select' and 'A: Confirm' just on the loader menu
    update_element_alpha(&all_element_alphas->text.three_columns.left_select, current_gameselect_state == SUBMENU_GAMESELECT_LOADER ? element_alpha_visible : element_alpha_hidden);
    update_element_alpha(&all_element_alphas->text.three_columns.right_confirm, current_gameselect_state == SUBMENU_GAMESELECT_LOADER ? element_alpha_visible : element_alpha_hidden);
}

static void update_gameplay_button_icons() {
    // Disable button icons used by the outer menu
    update_element_alpha(&all_element_alphas->icons.one_column.control_stick, element_alpha_hidden);
    update_element_alpha(&all_element_alphas->icons.two_columns.left_b_button, element_alpha_hidden);
    update_element_alpha(&all_element_alphas->icons.two_columns.right_a_button, element_alpha_hidden);

    // Show 'B: Cancel' on both the loader and start screens
    update_element_alpha(&all_element_alphas->icons.three_columns.centre_b_button, element_alpha_visible);

    // Show 'Control Stick: Select' and 'A: Confirm' just on the loader menu
    update_element_alpha(&all_element_alphas->icons.three_columns.left_control_stick, current_gameselect_state == SUBMENU_GAMESELECT_LOADER ? element_alpha_visible : element_alpha_hidden);
    update_element_alpha(&all_element_alphas->icons.three_columns.right_a_button, current_gameselect_state == SUBMENU_GAMESELECT_LOADER ? element_alpha_visible : element_alpha_hidden);
}

static void update_pal_button_languages() {
    if (!current_pal_buttons_language || !pal_button_language_elements) {
        // Only PAL 1.0 and 1.2 have runtime-configurable languages; other versions don't need this additional step
        return;
    }

    for (int i = 0; i < NUM_PAL_LANGUAGES; i++) {
        element_alpha_update_state_t language_state = (i == *current_pal_buttons_language) ? element_alpha_visible : element_alpha_hidden;
        update_element_alpha(&pal_button_language_elements[i], language_state);
    }
}

__attribute_used__ void patch_update_button_alphas() {
    bool ran_default_code = false;

    switch (*cur_menu_id) {
        case MENU_GAMESELECT_TRANSITION_ID:
            update_gameplay_button_text();
            update_gameplay_button_icons();
            break;

        default:
            update_button_alphas();
            ran_default_code = true;
            break;
    }

    if (!ran_default_code) {
        // If we didn't run the default code, we also have to take care of updating the per-language elements on PAL systems
        update_pal_button_languages();
    }
}
