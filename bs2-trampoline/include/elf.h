#ifndef __ELF_H
#define __ELF_H

#include <stdint.h>
#include "elf_abi.h"

int valid_elf_image(const uint8_t *addr);
unsigned int load_elf_image(const uint8_t *addr);

#endif
