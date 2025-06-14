#include <gctypes.h>

#include "attr.h"
#include "element_alpha.h"

__attribute_reloc__ void (*draw_buttons)(u8);
__attribute_reloc__ void (*update_button_alphas)();

__attribute_used__ void patch_draw_buttons(u8 buttons_alpha) {
    draw_buttons(buttons_alpha); // Always called with `buttons_alpha` of 255
}

__attribute_used__ void patch_update_button_alphas() {
    update_button_alphas();
}
