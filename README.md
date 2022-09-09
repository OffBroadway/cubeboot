# Flippyboot IPL

This project is a patching framework for 
Port of the marvelous iplboot - A minimal GameCube IPL, for the Flippyboot.

cubeboot acts as a patching harness for BS2 and is capable of mounting external
FAT devices and chainloading a DOL. The essential copyrighted BS1 and Font ROM 
are not provided, nor needed with the Flippyboot as these are resident onboard 
the Gamecube's U10 ROM.

cubeboot can inject the BS2 into an existing scrambled ROM image for simulation
purposes via `make dolphinipl.bin` in the ipl directory. You must provide the
original ROM image for injection. Again, this is only necessary for development 
and debugging.

## Features
- [x] Restore boot animation with iplboot features
- [x] Loading an alternative IPL from an SD Card
- [x] Support all NTSC and PAL IPL revisions
- [ ] Variants for SDGecko A/B and SD2SP2

- [ ] Settings loaded from an SD Card
- [ ] Custom GameCube animation colors (tested)
  - [ ] Random color each boot using RTC
- [ ] Custom Nintendo logo text replacment
- [ ] Force Progressive video modes

## Usage

This project contains all the scripts to build cubeboot using the latest 
devkitPPC and GCC.  Additionally, scripts are provided which scramble the BS2
image suitable for injection over the stock BS2 in the GCN.

## Compatibility

Known compatible IPL versions:
- NTSC 1.0
- NTSC 1.1 (sim + hardware verified)
- NTSC 1.2 (DOL-001 and DOL-101)
- PAL 1.0
- MPAL 1.1
- PAL 1.2

## TODO
- [ ] Create GitHub Actions for CI/CD
- [ ] Flashale firmware image for picoboot
- [ ] Flashale settings files for picoboot
