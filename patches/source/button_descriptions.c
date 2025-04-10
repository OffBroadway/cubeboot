#include "button_descriptions.h"

#include "menu.h"
#include "attr.h"

__attribute_reloc__ void (*update_element_alpha)(element_alpha_state_t* element_ptr, bool disabled);

// Locations to branch back to, after the patched switch statement entry
__attribute_reloc__ void* after_update_button_text_jump_table;
__attribute_reloc__ void* after_update_button_icons_jump_table;

__attribute_reloc__ all_element_alphas_t* all_element_alphas;

__attribute_used__ void update_gameplay_button_text() {
    // Disable button descriptions used by the outer menu
    update_element_alpha(&all_element_alphas->text.one_column.menu_selection, true);
    update_element_alpha(&all_element_alphas->text.two_columns.left_cancel, true);
    update_element_alpha(&all_element_alphas->text.two_columns.right_confirm, true);

    // And also disable button descriptions used by other menus
    update_element_alpha(&all_element_alphas->text.three_columns.left_change, true);
    update_element_alpha(&all_element_alphas->text.three_columns.centre_finish, true);

    // Show 'B: Cancel' on both the loader and start screens
    update_element_alpha(&all_element_alphas->text.three_columns.centre_cancel, false);

    // Show 'Control Stick: Select' and 'A: Confirm' just on the loader menu
    update_element_alpha(&all_element_alphas->text.three_columns.left_select, current_gameselect_state != SUBMENU_GAMESELECT_LOADER);
    update_element_alpha(&all_element_alphas->text.three_columns.right_confirm, current_gameselect_state != SUBMENU_GAMESELECT_LOADER);
}

__attribute_used__ void update_gameplay_button_icons() {
    // Disable button icons used by the outer menu
    update_element_alpha(&all_element_alphas->icons.one_column.control_stick, true);
    update_element_alpha(&all_element_alphas->icons.two_columns.left_b_button, true);
    update_element_alpha(&all_element_alphas->icons.two_columns.right_a_button, true);

    // Show 'B: Cancel' on both the loader and start screens
    update_element_alpha(&all_element_alphas->icons.three_columns.centre_b_button, false);

    // Show 'Control Stick: Select' and 'A: Confirm' just on the loader menu
    update_element_alpha(&all_element_alphas->icons.three_columns.left_control_stick, current_gameselect_state != SUBMENU_GAMESELECT_LOADER);
    update_element_alpha(&all_element_alphas->icons.three_columns.right_a_button, current_gameselect_state != SUBMENU_GAMESELECT_LOADER);
}

// Within the function that updates the alpha of button descriptions, we patch two jump table entries
// corresponding to the 'gameplay' menu state, so they point to these two assembly snippets -
// these run the new C functions, then jump to the code following its jump table
asm(
".global patched_update_gameplay_button_text\n"
"patched_update_gameplay_button_text:\n"
"   bl update_gameplay_button_text\n"
"   lis 3, after_update_button_text_jump_table@h\n"
"   ori 3, 3, after_update_button_text_jump_table@l\n"
"   lwz 3, 0(3)\n"
"   mtctr 3\n"
"   bctr\n"
);

asm(
".global patched_update_gameplay_button_icons\n"
"patched_update_gameplay_button_icons:\n"
"   bl update_gameplay_button_icons\n"
"   lis 3, after_update_button_icons_jump_table@h\n"
"   ori 3, 3, after_update_button_icons_jump_table@l\n"
"   lwz 3, 0(3)\n"
"   mtctr 3\n"
"   bctr\n"
);
