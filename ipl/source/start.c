#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "dol.h" 
#include "cubeboot_dol.h"

// credit to swiss-gc/cube/actionreplay/main.c

void _memcpy(void* dest, const void* src, int count) {
	char* tmp = (char*)dest,* s = (char*)src;
	while (count--)
		*tmp++ = *s++;
}

void _memset(void* s, int c, int count) {
	char* xs = (char*)s;
	while (count--)
		*xs++ = c;
}

int main() {
    int i;
    DOLHEADER *hdr = (DOLHEADER *)cubeboot_dol;

    // Inspect text sections to see if what we found lies in here
    for (i = 0; i < MAXTEXTSECTION; i++) {
        if (hdr->textAddress[i] && hdr->textLength[i]) {
            _memcpy((void*)hdr->textAddress[i], ((unsigned char*)cubeboot_dol) + hdr->textOffset[i], hdr->textLength[i]);
        }
    }

    // Inspect data sections (shouldn't really need to unless someone was sneaky..)
    for (i = 0; i < MAXDATASECTION; i++) {
        if (hdr->dataAddress[i] && hdr->dataLength[i]) {
            _memcpy((void*)hdr->dataAddress[i], ((unsigned char*)cubeboot_dol) + hdr->dataOffset[i], hdr->dataLength[i]);
        }
    }
    
    // Clear BSS
    _memset((void*)hdr->bssAddress, 0, hdr->bssLength);
    
    void (*entrypoint)();
    entrypoint = (void(*)())hdr->entryPoint;
    entrypoint();

    __builtin_unreachable();
}

void exit(int code) {
    __builtin_unreachable();
}

void __eabi() {}
