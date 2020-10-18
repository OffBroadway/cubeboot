#!/usr/bin/env python3

import sys

def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <original IPL> <scrambled_executable> <output>")

    with open(sys.argv[1], "rb") as f:
        ipl = bytearray(f.read())

    with open(sys.argv[2], "rb") as f:
        sexe = bytearray(f.read())

    #We reconstruct the IPL using the real IPL, replacing BS2
    ipl[0x800:len(sexe)+0x800] = sexe

    with open(sys.argv[3], "wb") as f:
        f.write(ipl)

if __name__ == "__main__":
    sys.exit(main())
