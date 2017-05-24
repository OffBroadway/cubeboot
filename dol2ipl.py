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

def unpack_dol(data):
    header = struct.unpack(">64I", data[:256])

    dol_min = min(a for a in header[18:36] if a)
    dol_max = max(a + s for a, s in zip(header[18:36], header[36:54]))

    img = bytearray(dol_max - dol_min)

    for offset, address, length in zip(header[:18], header[18:36], header[36:54]):
        img[address - dol_min:address + length - dol_min] = data[offset:offset + length]

    # Entry point, load address, memory image
    return header[56], dol_min, img

def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <original IPL> <DOL> <output>")

    with open(sys.argv[1], "rb") as f:
        ipl = bytearray(f.read())

    header = ipl[:256]
    bs1 = scramble(ipl[256:2 * 1024])

    if header:
        print(header.decode("ASCII"))

    with open(sys.argv[2], "rb") as f:
        dol = bytearray(f.read())

    entry, load, img = unpack_dol(dol)
    entry &= 0x017FFFFF
    entry |= 0x80000000
    load &= 0x017FFFFF
    size = len(img)

    print(f"Entry point:   0x{entry:0{8}X}")
    print(f"Load address:  0x{load:0{8}X}")
    print(f"Image size:    {size} bytes ({size // 1024}K)")

    if sys.argv[3].endswith(".gcb"):
        bs1[0x51C + 2:0x51C + 4] = struct.pack(">H", load >> 16)
        bs1[0x520 + 2:0x520 + 4] = struct.pack(">H", load & 0xFFFF)
        bs1[0x5D4 + 2:0x5D4 + 4] = struct.pack(">H", entry >> 16)
        bs1[0x5D8 + 2:0x5D8 + 4] = struct.pack(">H", entry & 0xFFFF)
        bs1[0x524 + 2:0x524 + 4] = struct.pack(">H", size >> 16)
        bs1[0x528 + 2:0x528 + 4] = struct.pack(">H", size & 0xFFFF)

        # Qoob specific
        npages = math.ceil((len(header) + len(bs1) + size) / 0x10000)
        header[0xFD] = npages
        print(f"Qoob blocks:   {npages}")

        # Put it all together
        out = header + scramble(bs1 + img)

        # Pad to a multiple of 64KB
        out += bytearray(npages * 0x10000 - len(out))

    elif sys.argv[3].endswith(".vgc"):
        if entry != 0x81300000 or load != 0x01300000:
            print("Invalid entry point and base address (must be 0x81300000)")
            return -1

        header = b"VIPR\x01\x02".ljust(16, b"\x00") + b"iplboot".ljust(16, b"\x00")
        out = header + scramble(bytearray(0x720) + img)[0x720:]

    else:
        print("Unknown output format")
        return -1

    with open(sys.argv[3], "wb") as f:
        f.write(out)

if __name__ == "__main__":
    sys.exit(main())
