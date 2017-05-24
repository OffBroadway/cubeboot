# iplboot

A minimal GameCube IPL

## Usage

Flash the latest build to your Qoob Pro or Viper as a BIOS.  
The `_lz` version is intended for the Qoob SX but I haven't figured out how to
flash it yet.

After flashing, place an ipl.dol file on your SD card and turn the Cube on, it
will load it right away. The IPL also acts as a server for emu_kidid's
[usb-load](https://github.com/emukidid/gc-usb-load), should you want to use it
for development purposes.
