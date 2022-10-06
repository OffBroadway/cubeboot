# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
