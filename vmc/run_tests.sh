#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only
# Host tests of the virtual memory card (no console, no emulator): the card model and stores behind a fake drive, the write-back journal,
# the card generator, the IPL popup actions, the physical-device probe, the T2 watch and the IPL decision on synthetic images.
# Optional: CBVMC_DOL=<a retail GameCube DOL> runs the scanner-based tests on a real executable too (none is shipped here).
set -euo pipefail
d=$(cd "$(dirname "$0")" && pwd)
cc_bin="${CC:-cc}"
out="${TMPDIR:-/tmp}/cbvmc-host-tests"
rm -rf "$out"; mkdir -p "$out"
M="$d/core"
inc=(-I"$d" -I"$M/include")
san=(-std=c99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Werror)
dol="${CBVMC_DOL:-}"; dolarg=(); [ -n "$dol" ] && [ -f "$dol" ] && dolarg=(--dol "$dol")

# card generator first; a 4 Mbit (0.5 MiB) card it makes is the golden image of the other tests
"$cc_bin" "${san[@]}" -Wno-unused-parameter "${inc[@]}" "$d/tests/cbvmc_cardgen_test.c" "$d/cbvmc_cardgen.c" "$d/cbvmc_ramstore.c" "$M/vmc/cubeboot_vmc.c" -o "$out/tg"
"$out/tg" "$out/generated16.raw" | tail -1
"$cc_bin" "${san[@]}" "${inc[@]}" "$d/tests/cbvmc_mkcard.c" "$d/cbvmc_cardgen.c" "$d/cbvmc_ramstore.c" "$M/vmc/cubeboot_vmc.c" -o "$out/mkcard"
"$out/mkcard" "$out/golden.raw" 4

# the card model and the EXI runtime behind a fake CPU
"$cc_bin" -std=c89 -Wall -Wextra -Werror -pedantic "${inc[@]}" -c "$d/cbvmc_words.c" -o "$out/words89.o"
"$cc_bin" "${san[@]}" "${inc[@]}" "$d/cbvmc_rt.c" "$d/cbvmc_ramstore.c" "$d/cbvmc_words.c" "$M/vmc/cubeboot_vmc.c" "$M/vmc/cubeboot_vmc_exi.c" "$d/tests/cbvmc_test.c" -o "$out/t"
"$out/t" ${dolarg[@]+"${dolarg[@]}"} "$out/card.raw" | tail -1

# write-back store over the Flippy file API (simulated drive)
wbsrc=("$d/tests/cbvmc_wb_test.c" "$d/cbvmc_wbstore.c" "$d/cbvmc_ramstore.c" "$M/vmc/cubeboot_vmc.c" "$M/store/cubeboot_flippy_api.c" "$M/store/cubeboot_vmc_flippy.c")
"$cc_bin" "${san[@]}" -Wno-unused-function "${inc[@]}" -I"$d/tests" "${wbsrc[@]}" -o "$out/tw"
"$out/tw" "$out/golden.raw" "$out/wb_dump.raw" | tail -1
"$cc_bin" "${san[@]}" -Wno-unused-function -DCUBEBOOT_FLIPPY_CHUNK_BYTES=2048u "${inc[@]}" -I"$d/tests" "${wbsrc[@]}" -o "$out/tw_chunk"
"$out/tw_chunk" "$out/golden.raw" "$out/wb_dump_chunk.raw" >/dev/null
cmp "$out/wb_dump.raw" "$out/wb_dump_chunk.raw"

# the T2 multi-executable watch on synthetic breakpoint frames (a DOL adds the real-executable session)
"$cc_bin" "${san[@]}" "${inc[@]}" "$d/tests/cbvmc_t2_test.c" "$d/cbvmc_t2.c" "$d/cbvmc_scan.c" "$d/cbvmc_words.c" "$d/cbvmc_rt.c" "$d/cbvmc_loader.c" "$d/cbvmc_ramstore.c" "$M/vmc/cubeboot_vmc.c" "$M/vmc/cubeboot_vmc_exi.c" -o "$out/tt2"
if [ -n "$dol" ]; then "$out/tt2" "$dol" | tail -1; fi
v=$(grep -A1 '^cbvmc_fix_tab:' "$d/cbvmc_vec.S" | tail -1 | tr -d ' \t' | sed 's/^\.long//; s/0x0*//g'); c=$(grep 'cbvmc_t2_ranges\[CBVMC_T2_NRANGES\] =' "$d/cbvmc_t2.c" | sed 's/.*{//; s/}.*//; s/ //g; s/0x8*0*//g; s/u//g')
[ "$v" = "$c" ] || { echo "cbvmc_fix_tab ($v) differs from cbvmc_t2_ranges ($c)" >&2; exit 1; }

# the IPL decision (needs a DOL as the stand-in for the IPL text), the read-only card store, the popup actions, two cards on one window
"$cc_bin" "${san[@]}" "${inc[@]}" "$d/tests/cbvmc_ipl_policy_test.c" "$d/cbvmc_ipl_policy.c" "$d/cbvmc_scan.c" "$d/cbvmc_words.c" -o "$out/tpol"
if [ -n "$dol" ]; then "$out/tpol" "$dol" | tail -1; fi
"$cc_bin" -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Werror "${inc[@]}" \
    "$d/tests/cbvmc_ipl_ro_test.c" "$d/cbvmc_wbstore.c" "$d/cbvmc_wbmux.c" "$d/cbvmc_ramstore.c" "$M/vmc/cubeboot_vmc.c" -o "$out/tro"
"$out/tro" "$out/golden.raw" | tail -1
"$cc_bin" -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Werror "${inc[@]}" \
    "$d/tests/cbvmc_ipl_menu_test.c" "$d/cbvmc_ipl_menu.c" "$d/cbvmc_wbstore.c" "$d/cbvmc_wbmux.c" "$d/cbvmc_ramstore.c" "$M/vmc/cubeboot_vmc.c" -o "$out/tmenu"
rm -rf "$out/menu_fs"; mkdir -p "$out/menu_fs"; cp "$out/golden.raw" "$out/menu_card.raw"
"$out/tmenu" "$out/menu_card.raw" "$out/menu_fs" | tail -1
"$cc_bin" "${san[@]}" -Wno-unused-parameter -Wno-pointer-to-int-cast "${inc[@]}" -DCBVMC_TEST_RAM \
    "$d/tests/cbvmc_wbmux_test.c" "$d/cbvmc_resident_ram.c" "$d/cbvmc_wbmux.c" "$d/cbvmc_wbstore.c" "$d/cbvmc_ramstore.c" "$M/vmc/cubeboot_vmc.c" -o "$out/tm"
"$out/tm" | tail -1

# physical-device probe against a model of the EXI registers
"$cc_bin" "${san[@]}" "${inc[@]}" "$d/tests/cbvmc_exiprobe_test.c" "$d/cbvmc_exiprobe.c" -o "$out/tx"
"$out/tx" | tail -1
echo "virtual memory card host tests passed."
