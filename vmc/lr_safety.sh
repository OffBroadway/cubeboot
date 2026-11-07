# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only
# Link-register safety for GCC code that runs inside a game or in the IPL (offline/LR_SAFETY.md).
# Sourced by the build scripts:   . "$root/offline/lr_safety.sh"
#   LR_CFLAGS            add to every C compile (-ffixed-lr: GCC never keeps data in LR)
#   lr_check_c OBJ...    C objects: fail on any LR use beyond the prologue/epilogue (mflr r0 / mtlr r0 of the RA)
#   lr_verify OBJ...     asm objects: fail only on a real hazard (RA in 4(entry sp) while LR holds data, own LR
#                        save word used, access below r1); asm may use LR on purpose
#   lr_check_libgcc MAP  verify the libgcc members the link pulled in (libgcc is not built with -ffixed-lr)
# PPC_PREFIX selects the binutils (default /opt/devkitpro/devkitPPC/bin/powerpc-eabi), like the build scripts.
LR_SAFETY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LR_CFLAGS=(-ffixed-lr)
lr_check_c() { python3 "$LR_SAFETY_DIR/lr_check.py" check "$@"; }
lr_verify() { python3 "$LR_SAFETY_DIR/lr_check.py" verify "$@"; }
lr_check_libgcc() { python3 "$LR_SAFETY_DIR/lr_check.py" libgcc "$@"; }
