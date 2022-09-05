#include "dol.h"

/*--- DOL Decoding functions -----------------------------------------------*/
/****************************************************************************
* DOLMinMax
*
* Calculate the DOL minimum and maximum memory addresses
****************************************************************************/
static void DOLMinMax(DOLHEADER * dol)
{
    int i;

    maxaddress = 0;
    minaddress = 0x87100000;

    /*** Go through DOL sections ***/
    /*** Text sections ***/
    for (i = 0; i < MAXTEXTSECTION; i++)
    {
        if (dol->textAddress[i] && dol->textLength[i])
        {
            if (dol->textAddress[i] < minaddress)
                minaddress = dol->textAddress[i];
            if ((dol->textAddress[i] + dol->textLength[i]) > maxaddress) 
                maxaddress = dol->textAddress[i] + dol->textLength[i];
        }
    }

    /*** Data sections ***/
    for (i = 0; i < MAXDATASECTION; i++)
    {
        if (dol->dataAddress[i] && dol->dataLength[i])
        {
            if (dol->dataAddress[i] < minaddress)
                minaddress = dol->dataAddress[i];
            if ((dol->dataAddress[i] + dol->dataLength[i]) > maxaddress)
                maxaddress = dol->dataAddress[i] + dol->dataLength[i];
        }
    }

    /*** And of course, any BSS section ***/
    if (dol->bssAddress)
    {
        if ((dol->bssAddress + dol->bssLength) > maxaddress)
            maxaddress = dol->bssAddress + dol->bssLength;
    }

    /*** Some OLD dols, Xrick in particular, require ~128k clear memory ***/
    maxaddress += 0x20000;
}

u32 DOLSize(DOLHEADER *dol)
{
    u32 sizeinbytes;
    int i;

    sizeinbytes = DOLHDRLENGTH;

    /*** Go through DOL sections ***/
    /*** Text sections ***/
    for (i = 0; i < MAXTEXTSECTION; i++)
    {
        if (dol->textOffset[i])
        {
            if ((dol->textOffset[i] + dol->textLength[i]) > sizeinbytes)
                sizeinbytes = dol->textOffset[i] + dol->textLength[i];
        }
    }

    /*** Data sections ***/
    for (i = 0; i < MAXDATASECTION; i++)
    {
        if (dol->dataOffset[i])
        {
            if ((dol->dataOffset[i] + dol->dataLength[i]) > sizeinbytes)
                sizeinbytes = dol->dataOffset[i] + dol->dataLength[i];
        }
    }

    /*** Return DOL size ***/
    return sizeinbytes;
}

/****************************************************************************
* DOLtoMRAM
*
* Moves the DOL from main memory to MRAM
*
* Pass in a memory pointer to a previously loaded DOL
****************************************************************************/
int DOLtoMRAM(unsigned char *dol) {
    u32 sizeinbytes;
    int i;

    /*** Get DOL header ***/
    dolhdr = (DOLHEADER *) dol;

    /*** First, does this look like a DOL? ***/
    if (dolhdr->textOffset[0] != DOLHDRLENGTH && dolhdr->textOffset[0] != 0x0620)	// DOLX style 
        return 0;

    /*** Get DOL stats ***/
    DOLMinMax(dolhdr);
    sizeinbytes = maxaddress - minaddress;

    /*** Move all DOL sections into ARAM ***/
    /*** Move text sections ***/
    for (i = 0; i < MAXTEXTSECTION; i++) {
        /*** This may seem strange, but in developing d0lLZ we found some with section addresses with zero length ***/
        if (dolhdr->textAddress[i] && dolhdr->textLength[i]) {
            memcpy((char *) ((dolhdr->textAddress[i] - minaddress) + PROG_ADDR), dol + dolhdr->textOffset[i], dolhdr->textLength[i]);
        }
    }

    /*** Move data sections ***/
    for (i = 0; i < MAXDATASECTION; i++) {
        if (dolhdr->dataAddress[i] && dolhdr->dataLength[i]) {
            memcpy((char *) ((dolhdr->dataAddress[i] - minaddress) + PROG_ADDR), dol + dolhdr->dataOffset[i], dolhdr->dataLength[i]);
        }
    }

    iprintf("LOADCMD entry=%08x, minaddr=%08x, size=%08x\n", dolhdr->entryPoint, minaddress, sizeinbytes);

	_entrypoint = dolhdr->entryPoint;
	_dst = minaddress;
	_src = PROG_ADDR;
	_len = sizeinbytes;

    return 0;
}
