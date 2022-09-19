# Booting from SD

This guide describes how cubeboot can be used on a system with iplboot installed
as the initial program. This includes PicoBoot devices with stock firmware as well
as Viper and Qoob devices with iplboot installed to internal flash.

## Install

To install for use with iplboot, simply rename your current `IPL.dol` to `boot.dol`,
this will usually be Swiss.
Download the most recent `cubeboot.dol` file from the GitHub releases page. Now rename 
the `cubeboot.dol` file you downloaded to `IPL.dol` and copy it to the SD Card.

You should also download the included `cubeboot.ini` and change any settings you'd
like before continuing. This settings file allows you to customize aspects of the 
boot process.

Once you have confirmed that the SD Card contains `IPL.dol`, `boot.dol` and `cubeboot.ini`
you are ready to go!

## Issues

Some SD cards do not work well with cubeboot and will cause the GameCube animation to play
before opening the menu. If the GameCube boots to the menu and it says "NO DISC" then you
should follow the following steps.

To fix the "NO DISC" issue, download the latest `fallback.bin` from the GitHub releases and
copy it to your SD Card. Then open the `cubeboot.ini` file and add the line `force_fallback = 1`

Once you do this you should be able run cubeboot without getting stuck on the GameCube menu.
