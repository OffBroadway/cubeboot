# Flippyboot IPL

This project is a patching framework for 
Port of the marvelous iplboot - A minimal GameCube IPL, for the Flippyboot.

cubeboot acts as a patching harness for BS2 capable of mounting the external
FAT devices and chainloading a DOL. The essential copyrighted BS1 and Font ROM 
are not provided, nor needed with the Flippyboot as these are resident onboard 
the Gamecube's U10 ROM.

cubeboot can inject the BS2 into an existing scrambled ROM image for simulation
purposes via `make dolphinipl.bin`.  You must provide the original ROM image for 
injection.  Again, this is only necessary for development and debugging.

## Features
- [x] Support all NTSC and PAL IPL revisions
- [x] Loading an alternative IPL from an SD Card
- [ ] (temp) Program Loading in bs2start
- [ ] Settings loaded from an SD Card
- [ ] Custom GameCube animation colors (tested)
- [ ] Custom Nintendo logo text replacment
- [ ] Force Progressive video modes
- [ ] Variants for SDGecko A/B and SD2SP2
- [ ] A burnable firmware for picoboot
- [ ] A burnable settings file for picoboot

## Usage

This project contains all the scripts to build flippybooy-ipl using the latest 
devkitPPC and GCC.  Additionally, scripts are provided which scramble the BS2
image suitable for injection over the stock BS2 in the GCN.

## Compatibility

Known compatible IPL versions:
- NTSC 1.0
- NTSC 1.1 (sim + hardware verified)
- NTSC 1.2
- PAL 1.0
- PAL 1.1
- PAL 1.2
