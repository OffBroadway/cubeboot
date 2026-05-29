#!/bin/sh
set -e

# Generates BTI textures from source PNGs
# (while SVGs are available too, they must be converted back to PNG first after any edits)
# Requires wimgt (within Wiimms SZS Tools, https://szs.wiimm.de) to be on the PATH
cd "$(dirname "$0")/../data"
wimgt copy --transform BTI.I4 --n-mipmaps 0 original_png/disc.png disc.bti
wimgt copy --transform BTI.I4 --n-mipmaps 0 original_png/flippydrive.png flippydrive.bti
wimgt copy --transform BTI.I4 --n-mipmaps 0 original_png/l_button.png l_button.bti
wimgt copy --transform BTI.I4 --n-mipmaps 0 original_png/r_button.png r_button.bti
