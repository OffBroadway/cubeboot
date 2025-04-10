#define _LANGUAGE_ASSEMBLY
#include "asm.h"

// Within the function that updates the alpha of button descriptions, we patch two jump table entries
// corresponding to the 'gameplay' menu state, so they point to these two assembly snippets - these
// run the new functions (in the corresponding .c file), then jump to the code following its jump table
.global patched_update_gameplay_button_text
.global patched_update_gameplay_button_icons

patched_update_gameplay_button_text:
   bl update_gameplay_button_text
   lis r3, after_update_button_text_jump_table@h
   ori r3, r3, after_update_button_text_jump_table@l
   lwz r3, 0(r3)
   mtctr r3
   bctr

patched_update_gameplay_button_icons:
   bl update_gameplay_button_icons
   lis r3, after_update_button_icons_jump_table@h
   ori r3, r3, after_update_button_icons_jump_table@l
   lwz r3, 0(r3)
   mtctr r3
   bctr
