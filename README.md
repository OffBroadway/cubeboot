# cubeboot

This project provides a program called cubeboot which is able to play the GameCube
boot animation. This is useful for some modchips like PicoBoot which skip the boot
sequence. This project allows you to restore and customize the boot animation with
custom colors and logos.

## Technical Details

This project is a patching framework for the GameCube IPL which is called BS2.
The project was originally intended to restore the Boot Animation on IPL Replacements.

cubeboot acts as a patching harness for BS2 and is capable of mounting external
FAT devices and chainloading a DOL file. The essential copyrighted are not provided,
but instead loaded at runtime from the Gamecube's onboard U10 ROM.

**To be clear** cubeboot does not contain any copyrighted materials. It is able to run
on any GameCube unit without any additional software. All assets are provided by the IPL
resident on the GameCube motherboard. This is simply a software instrumentation framrwork.

## Usage

If you are using cubeboot with an IPL replacement that is loaded with iplboot
(like PicoBoot) you can simple install cubeboot to an SD card by following the
[SD Booting](./docs/SD_Boot.md) tutorial.

If you would like to use cubeboot loaded directly to an RP2040 / Pico you can find 
builds on the release page in the `uf2` firmware format. There are also instructions
for installing in the [RP2040 Pico](./docs/RP2040_Boot.md) tutorial.

cubeboot also includes a fallback mode where it boot into iplboot after the GameCube
animation plays. This also fixes some SD card compatibility issues for some users.
The tutorial docs include details on how to enable fallback mode.

## Compiling

This project contains all the scripts to build cubeboot using the latest 
devkitPPC and GCC.  Additionally, scripts are provided which scramble the BS2
image suitable for injection over the stock BS2 in the GCN.

## Features
- Restore boot animation
- Loading an alternative IPL from an SD Card
- Support all NTSC and PAL IPL revisions
- Support booting SDGecko A/B and SD2SP2
- Flashable firmware image for picoboot
- Settings loaded from an SD Card
- Custom GameCube animation colors
  - Random color each boot using RTC
- Custom Nintendo logo text replacement
- Force Progressive video modes

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
- [ ] Add GCLoader SD Card support
- [ ] Flashable settings files for picoboot
- [ ] Add support for Memory Card boot.dol and ipl.bin
- [ ] iplboot like button-press features for DOLs
- [ ] More options for cube color
  - [ ] Individual color selection per-object
  - [ ] Control texture saturation
- [ ] Live rendering of `cube_text`
