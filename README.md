# iplboot (Flippyboot Edition)

Port of the marvelous iplboot - A minimal GameCube IPL, for the Flippyboot.

iplboot acts as a replacement BS2 capable of mounting the internal (or a 
select list of external) FAT devices and chainloading a DOL.  The essential
copyrighted BS1 and Font ROM are not provided, nor needed with the Flippyboot 
as these are resident onboard the Gamecube's U10 ROM.

iplboot can inject the BS2 into an existing scrambled ROM image for simulation
purposes via `make dolphinipl.bin`.  You must provide the original ROM image for 
injection.  Again, this is only necessary for development and debugging.

## Usage

This project contains all the scripts to build iplboot using the latest 
devkitPPC and GCC.  Additionally, scripts are provided which scramble the BS2
image suitable for injection over the stock BS2 in the GCN.

## Compatibility

Known compatible IPL versions:

- NTSC 1.1 (sim + hardware verified)
