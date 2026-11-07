#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only
"""Link-register safety check for GCC-built PowerPC code that runs inside a game or the IPL.  Background: offline/LR_SAFETY.md.

The Nintendo SDK's interrupt entries (external interrupt, decrementer) run a Metrowerks C function on the interrupted
stack whose prologue is `mflr r0; stw r0,4(r1); stwu r1,-N(r1)`: every interrupt writes the interrupted LR into the
word 4(r1) of the interrupted r1.  A function whose return address (RA) sits in that word while LR holds anything else
returns into that value if an interrupt hits the window (S6e console freeze: GCC frameless leaf mx_build kept a heap
pointer in LR).  This script finds such windows by a small data-flow analysis of each function's disassembly.

Per instruction it tracks: the frame state (r1 == entry SP, or entry SP - N after `stwu r1,-N(r1)`), whether LR holds
the RA, which registers hold the RA or the entry SP, and (backwards) whether the RA slot 4(entry SP) is still going to
be read.  Findings:
  HAZARD      LR may hold something other than the RA while r1 == entry SP and the RA slot is live (the freeze)
  ownslot     the function itself uses 4(r1) of its own frame (the interrupt may overwrite that word at any time)
  belowsp     the function accesses memory below r1 (the interrupt's frame is built there)
  lrdata      mflr/mtlr with a register other than r0, or mtlr of a value that is not the RA (LR used for data);
              harmless by itself when no HAZARD is reported, but forbidden in our C (we build with -ffixed-lr)
  retnotra    a return (blr) where LR may not be the RA (would be a plain bug)
  unreached   instructions the control-flow walk did not reach (count; their windows are not analysed)

usage:
  lr_check.py check  OBJ...     C objects: exit 1 on any HAZARD / ownslot / belowsp / lrdata / retnotra / unreached.
  lr_check.py verify FILE...    asm objects, libgcc members, linked ELFs: exit 1 on HAZARD / ownslot / belowsp only
                                (LR as data is allowed where no interrupt can corrupt the RA).
  lr_check.py report FILE...    print one line per function with any finding (no failure).
  lr_check.py libgcc MAP...     `verify` the libgcc archive members a link pulled in (read from the -Map file).
The objdump binary is $PPC_PREFIX-objdump (default /opt/devkitpro/devkitPPC/bin/powerpc-eabi-objdump).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

PREFIX = os.environ.get("PPC_PREFIX", "/opt/devkitpro/devkitPPC/bin/powerpc-eabi")
OBJDUMP = PREFIX + "-objdump"
SEC_RE = re.compile(r"^Disassembly of section (\S+):$")
FUNC_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
INSN_RE = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} ){4}\s*(\S+)\s*(.*)$")
RELOC_RE = re.compile(r"^\s+([0-9a-f]+): (R_PPC_\S+)\s+(\S+)")
MEM_RE = re.compile(r"^([rf]\d+),(-?\d+)\((r\d+)\)$")
TGT_RE = re.compile(r"(?:^|,)([0-9a-f]+)(?:<[^>]*>)?$")
VOLATILE = {"r0"} | {"r%d" % i for i in range(3, 13)}
X = frozenset(["X"])
RA = frozenset(["RA"])
ESP = frozenset(["ESP"])
MEMOPS = {"lwz", "stw", "lmw", "stmw", "lbz", "lhz", "lha", "stb", "sth", "lfd", "stfd", "lfs", "stfs", "lwzu",
          "stwu", "lfdu", "stfdu", "psq_l", "psq_st"}


class Insn:
    __slots__ = ("addr", "mn", "ops", "reloc")

    def __init__(self, addr, mn, ops):
        self.addr, self.mn, self.ops, self.reloc = addr, mn, ops, None


def disasm(path):
    """[(section, name, [Insn])] in file order, plus {(section, offset)} jump-table targets from data relocations."""
    out = subprocess.run([OBJDUMP, "-dr", path], check=True, capture_output=True, text=True).stdout
    funcs, sec, cur = [], None, None
    for ln in out.splitlines():
        m = SEC_RE.match(ln)
        if m:
            sec = m.group(1)
            continue
        m = FUNC_RE.match(ln)
        if m:
            cur = (sec, m.group(2), [])
            funcs.append(cur)
            continue
        m = INSN_RE.match(ln)
        if m and cur:
            ops = m.group(3).split("#")[0].strip().replace(" ", "")
            cur[2].append(Insn(int(m.group(1), 16), m.group(2), ops))
            continue
        m = RELOC_RE.match(ln)
        if m and cur and cur[2]:
            cur[2][-1].reloc = (m.group(2), m.group(3))
    # Jump tables: GCC emits R_PPC_REL32 entries (target - entry address) and adds the table base at run time,
    # so an entry at offset o of a table starting at K points to (addend - (o - K)); absolute tables use ADDR32.
    rel = subprocess.run([OBJDUMP, "-r", path], capture_output=True, text=True).stdout
    cur_sec, entries, bases = None, [], {}
    for ln in rel.splitlines():
        m = re.match(r"RELOCATION RECORDS FOR \[(\S+)\]:", ln)
        if m:
            cur_sec = m.group(1)
            continue
        p = ln.split()
        if len(p) != 3 or not p[1].startswith("R_PPC_"):
            continue
        s, _, off = p[2].partition("+")
        add = int(off, 16) if off else 0
        if p[1] in ("R_PPC_REL32", "R_PPC_ADDR32") and s.startswith(".text") and not cur_sec.startswith(".text"):
            entries.append((cur_sec, int(p[0], 16), p[1], s, add))
        elif p[1] in ("R_PPC_ADDR16_LO", "R_PPC_ADDR16_HA") and not s.startswith(".text"):
            bases.setdefault(s, set()).add(add)
    targets = set()
    for sec, o, typ, s, add in entries:
        if typ == "R_PPC_ADDR32":
            targets.add((s, add))
        else:
            k = max([b for b in bases.get(sec, {0}) if b <= o] or [0])
            targets.add((s, add - (o - k)))
    return funcs, targets


def tgt(ops):
    m = TGT_RE.search(ops)
    return int(m.group(1), 16) if m else None


def is_cond_return(mn):
    return mn.startswith("b") and mn.endswith("lr") and mn not in ("blr", "blrl") and not mn.endswith("lrl")


def is_table_dispatch(insns, i):
    """True if the bctr at i jumps through a table: mtctr rX where rX comes from lwzx (directly or via add)."""
    want = None
    for k in range(i - 1, max(-1, i - 16), -1):
        mn, ops = insns[k].mn, insns[k].ops
        if want is None:
            if mn == "mtctr":
                want = {ops}
            continue
        d = ops.split(",")[0] if ops else ""
        if d in want:
            if mn == "lwzx":
                return True
            if mn == "add":
                want = set(ops.split(",")[1:])
                continue
            return False
    return False


def cfg(insns, jt):
    idx = {ins.addr: i for i, ins in enumerate(insns)}
    n = len(insns)
    succ = [[] for _ in range(n)]
    for i, ins in enumerate(insns):
        mn, ops = ins.mn, ins.ops
        nxt = [i + 1] if i + 1 < n else []
        ext = ins.reloc is not None and ins.reloc[0] == "R_PPC_REL24"
        if mn in ("blr", "rfi"):
            continue
        if mn == "bctr":
            # a jump-table dispatch (CTR computed from an lwzx table load) goes to the table targets; any other
            # bctr is an indirect tail call and leaves the function
            succ[i] = sorted(idx[t] for t in jt if t in idx) if is_table_dispatch(insns, i) else []
        elif mn == "b":
            t = tgt(ops)
            if not ext and t in idx:
                succ[i] = [idx[t]]
        elif mn in ("bl", "blrl", "bctrl") or mn.endswith("lrl") or mn.endswith("ctrl"):
            succ[i] = nxt
        elif is_cond_return(mn) or mn.endswith("ctr"):
            succ[i] = nxt
        elif mn.startswith("b") and mn != "b":
            t = tgt(ops)
            succ[i] = nxt + ([idx[t]] if (t in idx and not ext) else [])
        else:
            succ[i] = nxt
    return succ


def rget(regs, r):
    for k, v in regs:
        if k == r:
            return v
    return X


def rset(regs, r, v):
    d = dict(regs)
    d[r] = v
    return tuple(sorted(d.items()))


def join(a, b):
    if a is None:
        return b
    da, db = dict(a[2]), dict(b[2])
    regs = tuple(sorted((k, da.get(k, X) | db.get(k, X)) for k in set(da) | set(db)))
    return (a[0] | b[0], a[1] | b[1], regs)


def analyse(insns, jt):
    """Forward state + backward RA-slot liveness over the function's control-flow graph."""
    n = len(insns)
    succ = cfg(insns, jt)
    find = {"HAZARD": set(), "ownslot": set(), "belowsp": set(), "lrdata": set(), "retnotra": set()}
    slot_rd, slot_wr = [False] * n, [False] * n

    def step(i, s):
        ins = insns[i]
        mn, ops = ins.mn, ins.ops
        fr, lr, regs = s
        a = "%x" % ins.addr
        m = MEM_RE.match(ops)
        if mn == "stwu" and m and m.group(1) == "r1" and m.group(3) == "r1":
            k = -int(m.group(2))
            return (frozenset(f + k if f != "U" else "U" for f in fr), lr, regs)
        if mn == "stwux" and ops.startswith("r1,r1,"):
            return (frozenset(["U"]), lr, regs)
        if mn == "addi" and ops.startswith("r1,r1,"):
            k = int(ops.split(",")[2])
            return (frozenset((f - k) if f != "U" else "U" for f in fr), lr, regs)
        if mn == "mr" and ops.startswith("r1,"):
            return (frozenset([0]) if rget(regs, ops.split(",")[1]) == ESP else frozenset(["U"]), lr, regs)
        if mn == "lwz" and ops == "r1,0(r1)":
            return (frozenset([0]), lr, regs)
        if mn == "mflr":
            if ops != "r0":
                find["lrdata"].add(a)
            return (fr, lr, rset(regs, ops, frozenset("RA" if x == "RA" else "X" for x in lr)))
        if mn == "mtlr":
            v = rget(regs, ops)
            if ops != "r0" or v != RA:
                find["lrdata"].add(a)
            return (fr, frozenset("RA" if x == "RA" else "X" for x in v), regs)
        if mn in ("bl", "blrl", "bctrl") or (mn.startswith("b") and (mn.endswith("lrl") or mn.endswith("ctrl"))):
            return (fr, X, tuple((k, v) for k, v in regs if k not in VOLATILE))
        if mn == "mr":
            d, s_ = ops.split(",")[:2]
            return (fr, lr, rset(regs, d, rget(regs, s_)))
        if mn == "addi" and re.match(r"r\d+,r1,-?\d+$", ops):
            d, _, k = ops.split(",")
            esp = all(f != "U" and f == int(k) for f in fr)
            return (fr, lr, rset(regs, d, ESP if esp else X))
        if m and mn in MEMOPS and m.group(3) in ("r1", "r11"):
            rt, off, base = m.group(1), int(m.group(2)), m.group(3)
            width = 4 * (32 - int(rt[1:])) if mn in ("lmw", "stmw") else (8 if "fd" in mn or mn.startswith("psq")
                                                                            else 4)
            load = mn.startswith("l") or mn == "psq_l"
            slot = False
            if base == "r1":
                for f in fr:
                    if f == "U":
                        continue
                    if off < 0 and not (mn == "stwu" and rt == "r1"):
                        find["belowsp"].add(a)
                    if f == 0 and off <= 4 < off + width:
                        slot = True
                    if f > 0 and off <= 4 < off + width:
                        find["ownslot"].add(a)
                    if f > 0 and off <= f + 4 < off + width:
                        slot = True
                if mn == "lwz" and off == 0 and rt != "r1":
                    return (fr, lr, rset(regs, rt, ESP if 0 not in fr else X))
            elif rget(regs, "r11") == ESP and off <= 4 < off + width:
                slot = True
            if slot:
                if load:
                    slot_rd[i] = True
                else:
                    slot_wr[i] = True
            if load and rt.startswith("r"):
                if mn == "lmw":
                    for r in range(int(rt[1:]), 32):
                        regs = rset(regs, "r%d" % r, X)
                    return (fr, lr, regs)
                return (fr, lr, rset(regs, rt, RA if slot else X))
            return s
        if mn == "blr" or is_cond_return(mn):
            if "X" in lr:
                find["retnotra"].add(a)
            return s
        if mn == "b" and ins.reloc is not None:
            # external tail call; libgcc's _restgpr_N_x helpers read the RA through r11 = entry SP
            if rget(regs, "r11") == ESP and ins.reloc[1].endswith("_x"):
                slot_rd[i] = True
            return s
        if mn == "lmw":
            for r in range(int(ops.split(",")[0][1:]), 32):
                regs = rset(regs, "r%d" % r, X)
            return (fr, lr, regs)
        if ops and not mn.startswith(("st", "cmp", "b", "mt", "tw", "dcb", "icb", "sync", "isync", "eieio", "cr",
                                      "mcrf", "mcrxr", "psq_st")):
            d = ops.split(",")[0]
            if re.fullmatch(r"r\d+", d):
                return (fr, lr, rset(regs, d, X))
        return s

    st = [None] * n
    st[0] = (frozenset([0]), RA, ())
    work, steps = [0], 0
    while work and steps < 500000:
        i = work.pop()
        steps += 1
        s2 = step(i, st[i])
        for j in succ[i]:
            nj = join(st[j], s2)
            if nj != st[j]:
                st[j] = nj
                work.append(j)
    live_in, changed = [False] * n, True
    while changed:
        changed = False
        for i in range(n - 1, -1, -1):
            li = slot_rd[i] or (any(live_in[j] for j in succ[i]) and not slot_wr[i])
            if li != live_in[i]:
                live_in[i], changed = li, True
    unreached = 0
    for i in range(n):
        if st[i] is None:
            unreached += 1
            continue
        fr, lr, _ = st[i]
        if 0 in fr and "X" in lr and live_in[i]:
            find["HAZARD"].add("%x" % insns[i].addr)
    res = {k: sorted(v, key=lambda x: int(x, 16)) for k, v in find.items() if v}
    if unreached:
        res["unreached"] = [str(unreached)]
    return res


def scan(path):
    funcs, targets = disasm(path)
    res = []
    for k, (sec, name, insns) in enumerate(funcs):
        if not insns:
            continue
        lo, hi = insns[0].addr, insns[-1].addr
        jt = {off for (s, off) in targets if s == sec and lo <= off <= hi}
        res.append((name, analyse(insns, jt)))
    return res


def libgcc_members(maps):
    """Extract the libgcc.a members named in link maps into a temp dir; return (dir, paths)."""
    mem, arch = set(), None
    for mp in maps:
        for ln in open(mp, errors="replace"):
            m = re.search(r"(\S*libgcc\.a)\(([^)]+)\)", ln)
            if m:
                arch = m.group(1)
                mem.add(m.group(2))
    if not mem:
        return None, []
    tmp = tempfile.mkdtemp(prefix="lrcheck-")
    subprocess.run([PREFIX + "-ar", "x", "--output", tmp, arch] + sorted(mem), check=True)
    return tmp, [os.path.join(tmp, x) for x in sorted(mem)]


def main(argv):
    if len(argv) < 3 or argv[1] not in ("check", "verify", "report", "libgcc"):
        print(__doc__, file=sys.stderr)
        return 2
    mode, files, tmp = argv[1], argv[2:], None
    if mode == "libgcc":
        tmp, files = libgcc_members(files)
        mode = "verify"
    fatal = {"check": ("HAZARD", "ownslot", "belowsp", "lrdata", "retnotra", "unreached"),
             "verify": ("HAZARD", "ownslot", "belowsp"), "report": ()}[mode]
    bad = 0
    try:
        for p in files:
            for name, f in scan(p):
                if not f:
                    continue
                hit = [k for k in fatal if k in f]
                bad += bool(hit)
                if mode == "report" or hit:
                    line = "%s: %s: %s" % (os.path.basename(p), name,
                                           " ".join("%s=%s" % (k, ",".join(v)) for k, v in f.items()))
                    print(line + ("  <-- FAIL" if hit else ""), file=sys.stdout if mode == "report" else sys.stderr)
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
    if bad:
        print("lr_check %s: %d function(s) fail (see offline/LR_SAFETY.md; C must be built with -ffixed-lr)" %
              (mode, bad), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
