#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "dol.h" 
#include "bs2-trampoline_dol.h"

// credit to swiss-gc/cube/actionreplay/main.c

void start() {
	int i;
	DOLHEADER *hdr = (DOLHEADER *)bs2_trampoline_dol;

	// Inspect text sections to see if what we found lies in here
	for (i = 0; i < MAXTEXTSECTION; i++) {
		if (hdr->textAddress[i] && hdr->textLength[i]) {
			memcpy((void*)hdr->textAddress[i], ((unsigned char*)bs2_trampoline_dol) + hdr->textOffset[i], hdr->textLength[i]);
		}
	}

	// Inspect data sections (shouldn't really need to unless someone was sneaky..)
	for (i = 0; i < MAXDATASECTION; i++) {
		if (hdr->dataAddress[i] && hdr->dataLength[i]) {
			memcpy((void*)hdr->dataAddress[i], ((unsigned char*)bs2_trampoline_dol) + hdr->dataOffset[i], hdr->dataLength[i]);
		}
	}
	
	// Clear BSS
	memset((void*)hdr->bssAddress, 0, hdr->bssLength);
	
	void (*entrypoint)();
	entrypoint = (void(*)())hdr->entryPoint;
	entrypoint();

    __builtin_unreachable();
}

// unused
int main() {
	return 0;
}
