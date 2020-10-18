#!/usr/bin/env python3

import math
import struct
import sys

# bootrom descrambler reversed by segher
def scramble(data):
    acc = 0
    nacc = 0

    t = 0x2953
    u = 0xD9C2
    v = 0x3FF1

    x = 1

    it = 0
    while it < len(data):
        t0 = t & 1
        t1 = (t >> 1) & 1
        u0 = u & 1
        u1 = (u >> 1) & 1
        v0 = v & 1

        x ^= t1 ^ v0
        x ^= u0 | u1
        x ^= (t0 ^ u1 ^ v0) & (t0 ^ u0)

        if t0 == u0:
            v >>= 1
            if v0:
                v ^= 0xB3D0

        if t0 == 0:
            u >>= 1
            if u0:
                u ^= 0xFB10

        t >>= 1
        if t0:
            t ^= 0xA740

        nacc = (nacc + 1) % 256
        acc = (acc * 2 + x) % 256
        if nacc == 8:
            data[it] ^= acc
            nacc = 0
            it += 1

    return data

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <executable> <output>")

    with open(sys.argv[1], "rb") as f:
        builtimg = bytearray(f.read())

    #We need prepend the bytes normally occupied by BS1
    scrambledimg = bytearray(0x700) + builtimg
    #Then scramble the whole thing
    scrambledimg = scramble(scrambledimg)
    #And remove those prepended bytes
    scrambledimg = scrambledimg[0x700:]

    with open(sys.argv[2], "wb") as f:
        f.write(scrambledimg)

if __name__ == "__main__":
    sys.exit(main())
