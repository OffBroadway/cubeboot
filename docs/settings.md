# Settings

These are all of the values supported by the `cubeboot.ini` file.

```
cube_color = 00ffff     # hex color code
cube_logo = path.png    # path to a 352x40px PNG image
force_progressive = 1   # enables progressive scan

[cubeboot]
# Virtual memory cards on the SD card (needs a FlippyDrive).  1 = on, 0 = off (nothing of this runs)
vmc = 1

# The folder that holds the card files.  Each slot has one card file per region
# (MemoryCardA.USA.raw, MemoryCardA.JAP.raw, ... and the same for slot B); a missing
# file is created on first use.  Point it at /swiss/saves to share the cards with
# Swiss's own memory card emulation (same file names).
vmc_dir = /cubeboot/saves
# vmc_dir = /swiss/saves

# How games and the IPL's Memory Card menu pick a card:
#   stock = as a real memory card: one pair of cards for US and PAL (the USA files), another for Japan (JAP)
#   swiss = as Swiss does: separate pairs for US (USA), European (EUR) and Japanese (JAP) games
# Use swiss when sharing a folder with Swiss, so that both see the same card for a PAL game.
vmc_regions = stock

# Size of a card file created when it is missing: 59, 123, 251, 507, 1019 or 2043 blocks.
# An existing card is never changed.
vmc_blocks = 2043

# Diagnostics.  0 = write nothing to the SD card.  1 = write four small status files under
# /cubeboot (what to attach to a support question).  Higher bits are developer switches.
vmc_diag = 0
```

See [virtual_memory_card.md](virtual_memory_card.md) for the three `vmc` keys.
