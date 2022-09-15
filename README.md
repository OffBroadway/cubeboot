# Flippyboot IPL

This project is a patching framework for the GameCube IPL which is called BS2
The project was originally intended to restore the Boot Animation on Flippyboot.
Now the project has been generalized and works on both Flippyboot and PicoBoot.

cubeboot acts as a patching harness for BS2 and is capable of mounting external
FAT devices and chainloading a DOL. The essential copyrighted BS1 and Font ROM 
are not provided, nor needed with the Flippyboot as these are resident onboard 
the Gamecube's U10 ROM.

cubeboot can inject the BS2 into an existing scrambled ROM image for simulation
purposes via `make dolphinipl.bin` in the ipl directory. You must provide the
original ROM image for injection. Again, this is only necessary for development
 and debugging.

## Usage

If you are using cubeboot with an IPL replacement that is loaded with iplboot
(like PicoBoot) you can simple install cubeboot to an SD card by following the
[SD Booting](./docs/SD_Boot.md) tutorial.

cubeboot also includes a fallback mode where it can load your DOL file before
the boot animation.

## Compiling

This project contains all the scripts to build cubeboot using the latest 
devkitPPC and GCC.  Additionally, scripts are provided which scramble the BS2
image suitable for injection over the stock BS2 in the GCN.

## Features
- [x] Restore boot animation
  - [ ] with iplboot features
- [x] Loading an alternative IPL from an SD Card
- [x] Support all NTSC and PAL IPL revisions
- [x] Support booting SDGecko A/B and SD2SP2
- [ ] Flashable firmware image for picoboot (gzip)
- [ ] Settings loaded from an SD Card
- [ ] Custom GameCube animation colors (tested)
  - [ ] Random color each boot using RTC
- [ ] Custom Nintendo logo text replacement
- [ ] Force Progressive video modes

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
- [ ] Add GCLoader support
- [ ] Flashable settings files for picoboot
- [ ] Add support for Memory Card boot.dol and ipl.bin

## Pre-release TODO
- [ ] Write tutorial documentation (Fiverr spanish translation)

