# NO LONGER MAINTAINED

Because the most recent devkitPPC builds generate huge code, I can no longer
make iplboot fit in Qoob SX flash. Other modchips still have a fair amount of
headroom, but some critical bugs have demotivated me from working on this
project anymore.

Feel free to file bug reports, but beware that I will ignore them. I will review
pull requests, but I won't release any new binaries.

# iplboot

A minimal GameCube IPL

## Usage

Flash the latest build to your Qoob Pro or Viper as a BIOS.  
The Qoob SX is currently not supported because it uses a very different process
to boot, and reverse-engineering efforts so far have been unsuccessful.

After flashing, place an ipl.dol file on your SD card and turn the Cube on, it
will load it right away. The IPL also acts as a server for emu_kidid's
[usb-load](https://github.com/emukidid/gc-usb-load), should you want to use it
for development purposes.

## Building

A specific setup is required to build iplboot:
- devkitPPC r26
- Latest libOGC compiled with dkPPC r26
- Clang (`ln -s /usb/bin/clang $DEVKITPPC/bin/powerpc-eabi-clang`)

Additionally, the only BS1 that is currently known to work is the one from PAL
1.0 IPLs (full ROM MD5: `0cdda509e2da83c85bfe423dd87346cc`).
