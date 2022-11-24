# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.4] - Boot Held Button Programs

### Details
This build includes custom program loading, PAL 480p and optional boot delays.

Usage:
- Held button with standard names like `B.dol` and `START.dol`
- Held buttons with custom names like `button_x = test.dol` set in `cubeboot.ini`
- Set delays before the boot logo using `preboot_delay_ms = 500` (time in milliseconds)
- Set delays after the boot logo using `postboot_delay_ms = 3000` (time in milliseconds)
- PAL IPL 1.0 (DOL-001 PAL consoles) Force Progressive with `force_progressive = 1` set in `cubeboot.ini`
- Specify a custom default DOL file to boot into with `default_program = swiss.dol` set in `cubeboot.ini`

Use-cases:
- Hold button to load alternative DOL files (this also loads associated `.cli` files)
- Set custom program per-button with `button_name = something.dol`
- When using GCVideo, you can set `preboot_delay_ms` to wait for your TV to sync to the input source
- The `postboot_delay_ms` setting exists exclusively for flair. It can help recover the feeling of waiting for a game to load
- If you do not want cubeboot to enumerate through names like `boot.dol` and `autoexec.dol` you can set your own default with the `default_program` setting

Expected behavior:
- When you have both `Y.dol` and `test.dol` on the SD with `button_y = test.dol` in `cubeboot.ini`, this should boot `test.dol`
- The GCLoader SD card will only be used when booting directly from GCLoader (please submit an **Issue** if you have trouble using GCLoader with PicoBoot)

This release includes the following enhancements:
- https://github.com/OffBroadway/flippyboot-ipl/issues/13
- https://github.com/OffBroadway/flippyboot-ipl/issues/7
- https://github.com/OffBroadway/flippyboot-ipl/issues/2
- https://github.com/OffBroadway/flippyboot-ipl/issues/3

It also fixes the following bugs:
- https://github.com/OffBroadway/flippyboot-ipl/issues/5

## [0.1.3] - PAL Progressive Scan hotfix
### Details

This is a small code change to allow Progressive Scan on all PAL IPL revisions.
It fixes a regression introduced in v0.1.2

## [0.1.2] - GCLoader Support
### Details

This update adds support for GCLoader. This release also fixes the ["slow boot animation" bug](https://github.com/OffBroadway/flippyboot-ipl/issues/5) on PAL consoles.

**Please note:** this is to be used in conjunction with a Game / Application loader like Swiss.

When installing Swiss **do not use** the GCLoader ISO. Instead use cubeboot as your boot.iso and grab the most recent `swiss_rXXXX.dol` from `swiss/DOL/` and rename that to `autoexec.dol` or `boot.dol` on the root of your SD card.

That this version changes boot order and fixes some bugs with SD cards (you may be able to disable fallback mode).

The order that cubeboot will search for devices now is: GCLoader, SD SD2SP2, SDGecko Slot B, SD Gecko Slot A

## [0.1.0] - Initial Alpha
### Details

Please note that this is an **alpha** release. Some features are missing and some have not been fully implemented.

If you notice any issues, you are encouraged to post in the [GitHub Issue Tracker](https://github.com/OffBroadway/flippyboot-ipl/issues). Please make sure your issue is not already posted to avoid duplicates.

Please take a look at the [README](./README.md) for an overview of the project. However if you are looking for install tutorials please follow the guides in the [docs folder](https://github.com/OffBroadway/flippyboot-ipl/tree/master/docs).

Guides:
- [SD Booting](./docs/SD_Boot.md)
- [RP2040 Pico](./docs/RP2040_Boot.md)


Spanish translations are also available in the [docs folder](https://github.com/OffBroadway/flippyboot-ipl/tree/master/docs).

Thanks again to my incredible team mate @ChrisPVille for all the help along the way.
I also need to thank @Extrems and @emukidid for their project `swiss-gc` which has served as a great source of sample code for this project.

Thank you for trying out our project, we hope you like it!
