#include "element_alpha.h"

#include "attr.h"

__attribute_reloc__ void (*update_element_alpha)(element_alpha_state_t *element, element_alpha_update_state_t new_state);
__attribute_reloc__ void (*get_element_alpha)(element_alpha_state_t *element, u16 *output_alpha, u32 *output_unknown);
