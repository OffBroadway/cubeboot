# BS2 Notes (old)

BS2 uses low-mem as heap for some reason
> EDIT: the reason is obvious -- it's a bootloader

Heap is at 0x80700000
There is a secondary heap at 0x81100000
Program is loaded at 0x81300000
High-mem is only used up to around 0x81600000

BS2 only needs ~5MB of Heap, so `heap_start` can safely be moved to 0x80800000 or 0x80803100 (or exactly up to ~0x80877FF0)
BS2 runs OSInit which clears high-mem up to 0x81700000

Patches for BS2 can safely be placed at 0x81700000
