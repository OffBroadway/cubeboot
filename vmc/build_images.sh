#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only
# Builds the virtual memory card images (PowerPC, devkitPPC).  Outputs in OUTDIR (default vmc/build); the committed copies live in patches/data.
#   build_images.sh blob    [OUTDIR]  -> blob.{bin,elf,map,nm}: the runtime blob at 0x80000C00 (real-mode EXI trap handler, card model, T2 watch)
#   build_images.sh loader  [OUTDIR]  -> loader-{resident,install}.{bin,elf,map,nm}: the game-path images (needs blob); loadertest = RAM card variant
#   build_images.sh iplblob [OUTDIR]  -> ipl/blob.*: the trap blob of the IPL path, linked at CBVMC_IPL_BASE
#   build_images.sh iplres  [OUTDIR]  -> ipl/res.*: the IPL path's resident + installer + popup extension (needs iplblob); iplrestest = RAM card variant
# Flags and checks as offline/census/build_census.sh (-ffixed-lr, soft float, no small data, LR checks, no FPU).
set -euo pipefail
d=$(cd "$(dirname "$0")" && pwd); root=$(cd "$d/.." && pwd)
P="${PPC_PREFIX:-/opt/devkitpro/devkitPPC/bin/powerpc-eabi}"
what=${1:?blob|loader|loadertest|iplblob|iplres|iplrestest}
MODEL="$d/core"
out="${2:-$root/vmc/build}"
mkdir -p "$out"
FLAGS=(-Os -mcpu=750 -ffreestanding -fno-builtin -msdata=none -G0 -fno-pic -msoft-float -ffixed-lr
  -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -std=gnu11)
cc() { "$P-gcc" "${FLAGS[@]}" $([[ ${CBVMC_BLOB_FULL:-0} == 1 ]] && echo -DCBVMC_FULL_BLOB) $([[ ${CBVMC_DIAG:-0} == 1 ]] && echo -DCBVMC_DIAG) $([[ ${CBVMC_DIAG:-0} == 2 ]] && echo -DCBVMC_STALL) ${CBVMC_EXTRA_CFLAGS:-} -Wall -Wextra -Wno-unused-parameter -Werror -I"$d" -I"$MODEL/include" "$@"; }
. "$d/lr_safety.sh"
# Symbols of another image are given one by one (--defsym), never with --just-symbols: that would also bind libc functions (memcpy, memset...) to the
# other image's copies, which do not exist yet when the install image runs before the blob is copied (found by the Dolphin split harness).
defsyms() { # defsyms NMFILE SYM...
  local nm="$1" a r=(); shift
  for sym in "$@"; do a=$(grep " $sym\$" "$nm" | head -1 | cut -d' ' -f1); [[ -n $a ]] || { echo "symbol $sym not in $nm" >&2; exit 1; }; r+=("-Wl,--defsym=$sym=0x$a"); done
  printf '%s\n' "${r[@]}"
}
fpu_check() { # no FPU instruction, no LR used for data, no small-data register
  for o in "$@"; do
    if "$P-objdump" -d "$o" | grep -E '\s(lfs|stfs|lfd|stfd|fmr|fadd|fadds|fmul|fctiw|frsp|fsub|fdiv|fcmp)' >/dev/null; then echo "FPU use in $o" >&2; exit 1; fi
    if "$P-objdump" -d "$o" | grep -E '\((r?2|r?13)\)' >/dev/null; then echo "small-data register use in $o" >&2; exit 1; fi
  done
}
if [[ $what == blob ]]; then
  # the lean blob of the loader builds carries the T2 multi-executable trigger (cbvmc_t2.h): breakpoint routing and the repair entry in cbvmc_vec.S,
  # its own layout (cbvmc_blob_t2.ld: state and entry outside the vector ranges an SDK OSExceptionInit rewrites).  The old full blob (CBVMC_BLOB_FULL=1,
  # single-image Dolphin build) keeps the previous layout and code.
  T2=(); BLD="$d/cbvmc_blob.ld"
  [[ ${CBVMC_BLOB_FULL:-0} == 1 ]] || { T2=(-DCBVMC_T2); BLD="$d/cbvmc_blob_t2.ld"; }
  cc -c "$d/cbvmc_rt.c" -o "$out/rt.o"
  cc -c "$d/cbvmc_ramstore.c" -o "$out/ramstore.o"
  cc "${T2[@]+"${T2[@]}"}" -c "$d/cbvmc_blob.c" -o "$out/blob.o"
  cc -c "$d/cbvmc_libc.c" -o "$out/libc.o"
  cc -c "$MODEL/vmc/cubeboot_vmc.c" -o "$out/model.o"
  cc -c "$MODEL/vmc/cubeboot_vmc_exi.c" -o "$out/model_exi.o"
  "$P-gcc" -mcpu=750 -msoft-float -Wa,-mregnames "${T2[@]+"${T2[@]}"}" -c "$d/cbvmc_vec.S" -o "$out/vec.o"
  "$P-gcc" -nostartfiles -nostdlib -Wl,-T,"$BLD" -Wl,--gc-sections -Wl,-Map,"$out/blob.map" \
    -Wl,--undefined=cbvmc_vec $([[ ${CBVMC_BLOB_FULL:-0} == 1 ]] && echo -Wl,--undefined=cbvmc_blob_bind) -Wl,--undefined=cbvmc_blob_bind_ext -Wl,--undefined=cbvmc_blob_bind_b \
    $([[ -n ${T2[*]-} ]] && echo -Wl,--undefined=cbvmc_fix_pending -Wl,--undefined=cbvmc_fix_src -Wl,--undefined=cbvmc_t2_fn -Wl,--undefined=cbvmc_depth -Wl,--undefined=cbvmc_depth_max -Wl,--undefined=cbvmc_diag_fn) \
    "$out/vec.o" "$out/blob.o" "$out/rt.o" "$out/ramstore.o" "$out/model.o" "$out/model_exi.o" "$out/libc.o" -lgcc -o "$out/blob.elf"
  "$P-objcopy" -O binary "$out/blob.elf" "$out/blob.bin"
  "$P-nm" -n "$out/blob.elf" > "$out/blob.nm"
  "$P-size" "$out"/rt.o "$out"/ramstore.o "$out"/blob.o "$out"/model.o "$out"/model_exi.o "$out"/libc.o "$out/blob.elf" >&2
  "$P-nm" -u "$out/blob.elf" | grep . && { echo "unresolved symbols in the blob" >&2; exit 1; } || true
  fpu_check "$out"/rt.o "$out"/ramstore.o "$out"/blob.o "$out"/model.o "$out"/model_exi.o "$out"/libc.o
  lr_check_c "$out"/rt.o "$out"/ramstore.o "$out"/blob.o "$out"/model.o "$out"/model_exi.o "$out"/libc.o >/dev/null
  lr_verify "$out/blob.elf" >/dev/null
  lr_check_libgcc "$out/blob.map" >/dev/null
  echo "blob: $out/blob.bin ($(wc -c < "$out/blob.bin") bytes) at 0x80000C00, end $(grep ' __cbvmc_blob_end' "$out/blob.nm" | cut -d' ' -f1)$([[ -n ${T2[*]-} ]] && echo ", T2 layout: code below 0x1800 ends $(grep ' __cbvmc_lo_end' "$out/blob.nm" | cut -d' ' -f1), entry $(grep ' cbvmc_vec$' "$out/blob.nm" | cut -d' ' -f1)")"
elif [[ $what == loader || $what == loadertest ]]; then
  # Resident image (kept for the whole game, linked at CBVMC_R_BASE) and install image (runs once inside OSInit, staged in the arena at
  # CBVMC_B_BASE).  loader = FlippyDrive card file; loadertest = RAM card for the Dolphin proof (needs the blob built first).
  [[ -f "$out/blob.elf" ]] || { echo "build the blob first" >&2; exit 1; }
  read -r rbase bbase rsize < <(python3 - "$d/cbvmc_plan.h" "${CBVMC_DIAG:-0}" <<'PY'
import re,sys
t=open(sys.argv[1]).read()
rs=int(re.search(r'CBVMC_R_SIZE_%s\s+(0x[0-9A-Fa-f]+)u' % ('DIAG' if sys.argv[2] in ('1','2') else 'PROD'),t).group(1),16)
print(hex(0x81800000-0x200-rs), re.search(r'CBVMC_B_BASE\s+(0x[0-9A-Fa-f]+)u',t).group(1), hex(rs))
PY
)
  L="$out/$what"; mkdir -p "$L"
  abs=$(cd "$out" && pwd)/blob.bin
  printf '\t.section .cbvmc_blob,"a"\n\t.balign 32\n\t.globl cbvmc_blob_image\ncbvmc_blob_image:\n\t.incbin "%s"\n\t.globl cbvmc_blob_image_end\ncbvmc_blob_image_end:\n' "$abs" > "$L/blobimg.S"
  if [[ $what == loader ]]; then
    DEFS="-DCBVMC_LOADER"; RDEFS="-DCUBEBOOT_FLIPPY_CHUNK_BYTES=2048u"
    RSRC=("$d/cbvmc_resident_flippy.c" "$d/cbvmc_wbstore.c" "$d/cbvmc_wbmux.c" "$MODEL/store/cubeboot_vmc_flippy.c" "$MODEL/store/cubeboot_flippy_api.c" "$MODEL/store/cubeboot_flippy_di.c" "$d/cbvmc_libc.c")
  else
    DEFS="-DCBVMC_LOADER -DCBVMC_TEST_RAM"; RDEFS="-DCBVMC_TEST_RAM"
    RSRC=("$d/cbvmc_resident_ram.c" "$d/cbvmc_wbstore.c" "$d/cbvmc_wbmux.c" "$d/cbvmc_ramstore.c" "$d/cbvmc_libc.c")
  fi
  # T2 (cbvmc_t2.h): the scanner, the word applier and the breakpoint handler are RESIDENT (they must still be there when the next executable starts)
  RSRC+=("$d/cbvmc_resident_t2.c" "$d/cbvmc_t2.c" "$d/cbvmc_scan.c" "$d/cbvmc_words.c")
  rm -f "$L"/r_*.su
  robj=(); for f in "${RSRC[@]}"; do o="$L/r_$(basename "${f%.c}").o"; CBVMC_EXTRA_CFLAGS="$RDEFS -fstack-usage" cc -c "$f" -o "$o"; robj+=("$o"); done
  CBVMC_EXTRA_CFLAGS="$DEFS" cc -c "$d/cbvmc_install_split.c" -o "$L/i_install.o"
  iobj=("$L/i_install.o")
  for f in "$d/cbvmc_words.c" "$d/cbvmc_libc.c" "$d/cbvmc_cardgen.c" "$MODEL/vmc/cubeboot_vmc.c" "$MODEL/vmc/cubeboot_vmc_exi.c"; do
    o="$L/i_$(basename "${f%.c}").o"; CBVMC_EXTRA_CFLAGS="$DEFS" cc -c "$f" -o "$o"; iobj+=("$o"); done
  "$P-gcc" -mcpu=750 -msoft-float -Wa,-mregnames -DCBVMC_LOADER -I"$d" -c "$d/cbvmc_tramp.S" -o "$L/i_tramp.o"
  "$P-gcc" -mcpu=750 -c "$L/blobimg.S" -o "$L/i_blobimg.o"
  keep=(); for sym in cbvmc_resident_open cbvmc_resident_recover cbvmc_resident_prepare cbvmc_resident_note cbvmc_resident_trace_hook cbvmc_resident_tick cbvmc_resident_tick_context cbvmc_resident_sync cbvmc_resident_stack_top cbvmc_resident_card_path cbvmc_card_path cbvmc_card_path_b cbvmc_resident_port_b cbvmc_resident_slot_allow cbvmc_resident_card_geom cbvmc_slot_allow cbvmc_card_geom cbvmc_diag cbvmc_resident_t2_arm cbvmc_t2_enable cbvmc_diag_files cbvmc_t2_state; do keep+=("-Wl,--undefined=$sym"); done
  [[ $what == loadertest ]] && keep+=("-Wl,--undefined=cbvmc_test_stages" "-Wl,--undefined=cbvmc_test_stages_n")
  [[ $what == loadertest ]] && keep+=("-Wl,--undefined=cbvmc_test_card" "-Wl,--undefined=cbvmc_test_card_cap" "-Wl,--undefined=cbvmc_test_res" "-Wl,--undefined=cbvmc_test_wal" "-Wl,--undefined=cbvmc_test_card_len" "-Wl,--undefined=cbvmc_test_card_b" "-Wl,--undefined=cbvmc_test_card_b_cap" "-Wl,--undefined=cbvmc_test_card_b_len" "-Wl,--undefined=cbvmc_test_wal_b")
  "$P-gcc" -nostartfiles -nostdlib -Wl,-T,"$d/cbvmc_resident.ld" -Wl,-Ttext="$rbase" -Wl,--gc-sections -Wl,-Map,"$out/$what-resident.map" "${keep[@]}" \
    $(defsyms "$out/blob.nm" cbvmc_state cbvmc_chain cbvmc_t2_fn cbvmc_fix_pending cbvmc_fix_src cbvmc_decode cubeboot_vmc_exi_init cubeboot_vmc_card_select cbvmc_depth cbvmc_depth_max cbvmc_diag_fn cubeboot_vmc_exi_irq) \
    "${robj[@]}" -lgcc -o "$out/$what-resident.elf"
  "$P-objcopy" -O binary "$out/$what-resident.elf" "$out/$what-resident.bin"
  "$P-nm" -n "$out/$what-resident.elf" > "$out/$what-resident.nm"
  "$P-gcc" -nostartfiles -nostdlib -Wl,-T,"$d/cbvmc_install.ld" -Wl,-Ttext="$bbase" -Wl,--gc-sections -Wl,-Map,"$out/$what-install.map" \
    $(defsyms "$out/blob.nm" cbvmc_vec cbvmc_chain cbvmc_state __cbvmc_blob_end cbvmc_blob_bind_ext cbvmc_blob_bind_b) \
    $(defsyms "$out/$what-resident.nm" cbvmc_resident_open cbvmc_resident_recover cbvmc_resident_prepare cbvmc_resident_note cbvmc_resident_trace_hook cbvmc_resident_tick cbvmc_resident_tick_context cbvmc_resident_sync cbvmc_resident_stack_top cbvmc_resident_card_path cbvmc_resident_port_b cbvmc_resident_slot_allow cbvmc_resident_card_geom cbvmc_diag cbvmc_resident_t2_arm cbvmc_t2_enable cbvmc_diag_files $([[ $what == loadertest ]] && echo cbvmc_test_res)) \
    -Wl,--undefined=cbvmc_entry -Wl,--undefined=cbvmc_osreport \
    "$L/i_tramp.o" "${iobj[@]}" "$L/i_blobimg.o" -lgcc -o "$out/$what-install.elf"
  "$P-objcopy" -O binary "$out/$what-install.elf" "$out/$what-install.bin"
  "$P-nm" -n "$out/$what-install.elf" > "$out/$what-install.nm"
  "$P-size" "${robj[@]}" "$out/$what-resident.elf" >&2
  "$P-size" "${iobj[@]}" "$L/i_tramp.o" "$out/$what-install.elf" >&2
  for e in "$out/$what-resident.elf" "$out/$what-install.elf"; do "$P-nm" -u "$e" | grep . && { echo "unresolved symbols in $e" >&2; exit 1; } || true; done
  fpu_check "${robj[@]}" "${iobj[@]}"
  lr_check_c "${robj[@]}" "${iobj[@]}" >/dev/null
  lr_verify "$out/$what-resident.elf" >/dev/null; lr_verify "$out/$what-install.elf" >/dev/null
  lr_check_libgcc "$out/$what-resident.map" >/dev/null; lr_check_libgcc "$out/$what-install.map" >/dev/null
  printf '/* generated by build_blob.sh: address of the card path variable inside the resident image */\n#define CBVMC_R_CARD_PATH_ADDR 0x%sU\n#define CBVMC_R_CARD_PATH_MAX %uU\n#define CBVMC_R_SLOT_ALLOW_ADDR 0x%sU\n#define CBVMC_R_CARD_PATH_B_ADDR 0x%sU\n#define CBVMC_R_DIAG_ADDR 0x%sU\n#define CBVMC_R_T2_ENABLE_ADDR 0x%sU\n#define CBVMC_R_CARD_GEOM_ADDR 0x%sU\n' "$(grep ' cbvmc_card_path$' "$out/$what-resident.nm" | cut -d' ' -f1)" 88 "$(grep ' cbvmc_slot_allow$' "$out/$what-resident.nm" | cut -d' ' -f1)" "$(grep ' cbvmc_card_path_b$' "$out/$what-resident.nm" | cut -d' ' -f1)" "$(grep ' cbvmc_diag$' "$out/$what-resident.nm" | cut -d' ' -f1)" "$(grep ' cbvmc_t2_enable$' "$out/$what-resident.nm" | cut -d' ' -f1)" "$(grep ' cbvmc_card_geom$' "$out/$what-resident.nm" | cut -d' ' -f1)" > "$out/$what-resident-syms.h"
  rend=$(grep ' __cbvmc_res_end' "$out/$what-resident.nm" | cut -d' ' -f1); iend=$(grep ' __cbvmc_img_end' "$out/$what-install.nm" | cut -d' ' -f1)
  rneed=$(( 0x$rend - rbase )); ineed=$(( 0x$iend - bbase + 0xA000 ))
  (( rneed <= rsize )) || { printf "resident image ends %d bytes in, CBVMC_R_SIZE is %d\n" "$rneed" "$rsize" >&2; exit 1; }
  (( ineed <= 0xF000 )) || { printf "install image plus stack %d bytes, install region before the hook block is 61440\n" "$ineed" >&2; exit 1; }
  # the DSI handler stack (CBVMC_HSTACK_BYTES): the T2 path is handler -> cbvmc_t2_trap -> new_stage -> cbvmc_scan_ex -> follow (or the commit chain): sum of those frames + margin
  t2stack=$(cat "$L"/r_cbvmc_t2.su "$L"/r_cbvmc_scan.su "$L"/r_cbvmc_resident_t2.su 2>/dev/null | awk '/cbvmc_t2_trap|new_stage|cbvmc_scan_ex|:follow|t2_entry/ {s += $(NF-1)} END {print s+0}')
  hst=$(sed -n 's/^#define CBVMC_HSTACK_BYTES \([0-9]*\)u.*/\1/p' "$d/cbvmc_resident.h")
  (( t2stack + 512 <= hst )) || { echo "T2 scan path needs $t2stack bytes of handler stack (+512 margin), CBVMC_HSTACK_BYTES is $hst" >&2; exit 1; }
  echo "$what: resident $(wc -c < "$out/$what-resident.bin") bytes at $rbase (region $rsize, slack $((rsize - rneed))), install $(wc -c < "$out/$what-install.bin") bytes + 40960 stack at $bbase (region 65536, slack $((0x10000 - ineed))), T2 scan path stack $t2stack of $hst"
elif [[ $what == iplblob || $what == iplres || $what == iplrestest ]]; then
  # G4 stage 1 (docs/virtual_memory_card.md): the same runtime core (rt, model), but a different real-mode entry (cbvmc_vec_ipl.S: SPRG0 + stwx instead of
  # 16-bit absolute stores) so that the blob can live in the top of the Cubeboot patch window instead of 0x80000C00.  The game-path blob/resident/install are NOT touched.
  ibase=${CBVMC_IPL_BASE:-0x817C0000}
  I="$out/ipl"; mkdir -p "$I"
  IFL=(-DCBVMC_IPL_BASE=${ibase}u)
  if [[ $what == iplblob ]]; then
    cc "${IFL[@]}" -c "$d/cbvmc_rt.c" -o "$I/rt.o"
    cc "${IFL[@]}" -c "$d/cbvmc_ramstore.c" -o "$I/ramstore.o"
    cc "${IFL[@]}" -c "$d/cbvmc_blob.c" -o "$I/blob.o"
    cc "${IFL[@]}" -c "$d/cbvmc_libc.c" -o "$I/libc.o"
    cc "${IFL[@]}" -c "$MODEL/vmc/cubeboot_vmc.c" -o "$I/model.o"
    cc "${IFL[@]}" -c "$MODEL/vmc/cubeboot_vmc_exi.c" -o "$I/model_exi.o"
    "$P-gcc" -mcpu=750 -msoft-float -Wa,-mregnames -c "$d/cbvmc_vec_ipl.S" -o "$I/vec.o"
    "$P-gcc" -nostartfiles -nostdlib -Wl,-T,"$d/cbvmc_blob_ipl.ld" -Wl,--defsym=__cbvmc_ipl_base=$ibase -Wl,--gc-sections -Wl,-Map,"$I/blob.map" \
      -Wl,--undefined=cbvmc_vec -Wl,--undefined=cbvmc_blob_bind_ext -Wl,--undefined=cbvmc_blob_bind_b \
      "$I/vec.o" "$I/blob.o" "$I/rt.o" "$I/ramstore.o" "$I/model.o" "$I/model_exi.o" "$I/libc.o" -lgcc -o "$I/blob.elf"
    "$P-objcopy" -O binary "$I/blob.elf" "$I/blob.bin"
    python3 - "$I/blob.bin" <<'PY'
# the Cubeboot patch zeroes the whole region before it copies the image: trailing zero bytes (bss, zero-initialised data) need not be carried in the patch image
import sys
p = sys.argv[1]; d = open(p, "rb").read(); n = (len(d.rstrip(b"\0")) + 31) & ~31
open(p, "wb").write(d[:n])
PY
    "$P-nm" -n "$I/blob.elf" > "$I/blob.nm"
    "$P-nm" -u "$I/blob.elf" | grep . && { echo "unresolved symbols in the IPL blob" >&2; exit 1; } || true
    fpu_check "$I"/rt.o "$I"/ramstore.o "$I"/blob.o "$I"/model.o "$I"/model_exi.o "$I"/libc.o
    lr_check_c "$I"/rt.o "$I"/ramstore.o "$I"/blob.o "$I"/model.o "$I"/model_exi.o "$I"/libc.o >/dev/null
    lr_verify "$I/blob.elf" >/dev/null
    lr_check_libgcc "$I/blob.map" >/dev/null
    echo "iplblob: $I/blob.bin ($(wc -c < "$I/blob.bin") bytes) at $ibase, end $(grep ' __cbvmc_blob_end' "$I/blob.nm" | cut -d' ' -f1), vec $(grep ' cbvmc_vec$' "$I/blob.nm" | cut -d' ' -f1)"
  else
    [[ -f "$I/blob.elf" ]] || { echo "build iplblob first" >&2; exit 1; }
    T=res; DEFS=(); RDEF=(-DCUBEBOOT_FLIPPY_CHUNK_BYTES=2048u)
    RSRC=("$d/cbvmc_resident_ipl.c" "$d/cbvmc_install_ipl.c" "$d/cbvmc_words.c" "$d/cbvmc_libc.c" "$d/cbvmc_wbstore.c" "$d/cbvmc_wbmux.c" "$d/cbvmc_cardgen.c" "$d/cbvmc_ipl_menu.c" "$MODEL/vmc/cubeboot_vmc.c" "$MODEL/vmc/cubeboot_vmc_exi.c"
          "$MODEL/store/cubeboot_vmc_flippy.c" "$MODEL/store/cubeboot_flippy_api.c" "$MODEL/store/cubeboot_flippy_di.c")
    if [[ $what == iplrestest ]]; then
      T=restest; DEFS=(-DCBVMC_TEST_RAM); RDEF=(-DCBVMC_TEST_RAM)
      RSRC=("$d/cbvmc_resident_ipl.c" "$d/cbvmc_install_ipl.c" "$d/cbvmc_words.c" "$d/cbvmc_libc.c" "$d/cbvmc_wbstore.c" "$d/cbvmc_wbmux.c" "$d/cbvmc_cardgen.c" "$d/cbvmc_ipl_menu.c" "$d/cbvmc_ramstore.c" "$MODEL/vmc/cubeboot_vmc.c" "$MODEL/vmc/cubeboot_vmc_exi.c")
    fi
    robj=(); for f in "${RSRC[@]}"; do o="$I/${T}_$(basename "${f%.c}").o"; CBVMC_EXTRA_CFLAGS="${RDEF[*]} -fstack-usage" cc "${IFL[@]}" -c "$f" -o "$o"; robj+=("$o"); done
    "$P-gcc" -mcpu=750 -msoft-float -Wa,-mregnames -c "$d/cbvmc_ipl_tbl.S" -o "$I/${T}_tbl.o"
    sobj=(); if [[ $what == iplres ]]; then "$P-gcc" -mcpu=750 -msoft-float -Wa,-mregnames -c "$d/cbvmc_ipl_menu_stubs.S" -o "$I/${T}_menu_stubs.o"; sobj+=("$I/${T}_menu_stubs.o"); fi   # asm: checked by lr_verify on the ELF, not by the C rules
    rbase=$(( ibase + 0x2000 ))
    keepsym=(-Wl,--undefined=cbvmc_ipl_install_c -Wl,--undefined=cbvmc_ipl_finish_c -Wl,--undefined=cbvmc_ipl_replug_c)
    [[ $what == iplrestest ]] && keepsym+=(-Wl,--undefined=cbvmc_test_card -Wl,--undefined=cbvmc_test_card_len -Wl,--undefined=cbvmc_test_card_b -Wl,--undefined=cbvmc_test_card_b_len -Wl,--undefined=cbvmc_ripl_stat)
    "$P-gcc" -nostartfiles -nostdlib -Wl,-T,"$d/cbvmc_resident_ipl.ld" -Wl,-Ttext=$(printf '0x%X' $rbase) -Wl,--gc-sections -Wl,-Map,"$I/$T.map" "${keepsym[@]}" \
      $(defsyms "$I/blob.nm" cbvmc_state cbvmc_vec cbvmc_blob_bind_ext cbvmc_blob_bind_b) "$I/${T}_tbl.o" "${robj[@]}" "${sobj[@]}" -lgcc -o "$I/$T.elf"
    "$P-objcopy" -O binary "$I/$T.elf" "$I/$T.bin"
    python3 - "$I/$T.bin" <<'PY'
# the Cubeboot patch zeroes the whole region before it copies the image: trailing zero bytes (bss, zero-initialised data) need not be carried in the patch image
import sys
p = sys.argv[1]; d = open(p, "rb").read(); n = (len(d.rstrip(b"\0")) + 31) & ~31
open(p, "wb").write(d[:n])
PY
    "$P-nm" -n "$I/$T.elf" > "$I/$T.nm"
    "$P-nm" -u "$I/$T.elf" | grep . && { echo "unresolved symbols in the IPL $T image" >&2; exit 1; } || true
    "$P-size" "${robj[@]}" "$I/$T.elf" >&2
    fpu_check "${robj[@]}"
    lr_check_c "${robj[@]}" >/dev/null
    lr_verify "$I/$T.elf" >/dev/null
    lr_check_libgcc "$I/$T.map" >/dev/null
    first=$(head -c 8 "$I/$T.bin" | xxd -p)
    rend=$(grep ' __cbvmc_res_end' "$I/$T.nm" | cut -d' ' -f1); rneed=$(( 0x$rend - rbase ))
    irsize=$(( $(grep -o 'define CBVMC_IPL_RES_SIZE *0x[0-9A-Fa-f]*' "$d/cbvmc_ipl_plan.h" | grep -o '0x[0-9A-Fa-f]*') ))
    (( rneed <= irsize )) || { printf "IPL resident image ends %d bytes in, CBVMC_IPL_RES_SIZE is %d\n" "$rneed" "$irsize" >&2; exit 1; }
    maxstack=$(cat "$I"/${T}_*.su 2>/dev/null | awk '{print $(NF-1)}' | sort -n | tail -1)
    echo "ipl$T: resident+installer $(wc -c < "$I/$T.bin") bytes at $(printf '0x%X' $rbase) (region $irsize, slack $((irsize - rneed))), largest single frame ${maxstack:-?} bytes, entry table $first"
  fi
else
  echo "usage: $0 blob|loader|loadertest|iplblob|iplres|iplrestest [OUTDIR]" >&2; exit 2
fi
