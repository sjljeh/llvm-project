#!/usr/bin/env python3
"""Every-PC unwind checker for NTUW objects.

For each 8-byte .pdata entry of an Alpha, MIPS or PowerPC COFF object that
uses NTUW, the function is executed symbolically over its control-flow graph
from its entry, using an instruction model that is independent of LLVM's
encoder and of NTUWRuleTracker. At every reachable PC, the rules the record
gives for that code offset are applied to the symbolic state, and the result
must restore the caller exactly: sp, the return address, every nonvolatile
integer and floating-point register, and CR on PowerPC. Wherever the language
handler would run, the establisher frame must be a single frame-relative
value. At every return and every tail call, the machine state itself must be
the caller's.

A function split into chained records (UNW_ChainInfo) is walked as one: at
a PC in a chained fragment, the parent's body rules apply, followed by the
fragment's own epilogue scopes.

usage: ntuw-check.py [-v] obj [function...]
Exits with status 1 if any function fails.
"""
import struct
import sys

MACH_ALPHA, MACH_ALPHA64, MACH_MIPS, MACH_PPC = 0x184, 0x284, 0x166, 0x1F0

# ------------------------------------------------------------------ COFF
class Coff:
    def __init__(self, path):
        d = self.d = open(path, "rb").read()
        (self.machine, nsec, _, symptr, nsym, opt, _) = struct.unpack_from("<HHIIIHH", d, 0)
        strtab = symptr + nsym * 18
        def name(raw):
            if raw[:4] == b"\0\0\0\0":
                off = struct.unpack_from("<I", raw, 4)[0]
                return d[strtab + off:d.index(b"\0", strtab + off)].decode()
            return raw.rstrip(b"\0").decode()
        self.secs = []
        for i in range(nsec):
            o = 20 + opt + i * 40
            raw = d[o:o + 8]
            n = raw.rstrip(b"\0").decode()
            if n.startswith("/"):
                off = int(n[1:])
                n = d[strtab + off:d.index(b"\0", strtab + off)].decode()
            (vsz, va, rsz, rptr, relptr, _, nrel, _, ch) = struct.unpack_from("<IIIIIIHHI", d, o + 8)
            self.secs.append(dict(name=n, data=d[rptr:rptr + rsz], relptr=relptr, nrel=nrel))
        self.syms = []
        i = 0
        while i < nsym:
            o = symptr + i * 18
            (val, sec, typ, cls, naux) = struct.unpack_from("<IhHBB", d, o + 8)
            self.syms.append(dict(name=name(d[o:o + 8]), value=val, sec=sec, cls=cls))
            for _ in range(naux):
                self.syms.append(None)
            i += 1 + naux
        for s in self.secs:
            s["relocs"] = {}
            for r in range(s["nrel"]):
                (off, si, typ) = struct.unpack_from("<IIH", d, s["relptr"] + r * 10)
                s["relocs"].setdefault(off, []).append((si, typ))

    def word_at(self, sec, off):
        """Resolve an absolute 32-bit word: (section index, offset) or name."""
        s = self.secs[sec]
        raw = struct.unpack_from("<I", s["data"], off)[0]
        for si, typ in s["relocs"].get(off, []):
            sym = self.syms[si]
            if sym["sec"] > 0:
                return (sym["sec"] - 1, sym["value"] + raw), sym["name"]
            return None, sym["name"]
        return None, raw if raw else None

    def func_name(self, sec, off):
        best = None
        for s in self.syms:
            if s and s["sec"] - 1 == sec and s["value"] == off and s["cls"] in (2, 3):
                if s["name"] == self.secs[sec]["name"]:
                    continue  # the section symbol
                if best is None or not s["name"].startswith((".", "$")):
                    best = s["name"]
        return best or "%s+0x%x" % (self.secs[sec]["name"], off)


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


# ------------------------------------------------------------ symbolic values
# ('S', k)  entry sp + k            ('E', r)  entry value of register r
# ('C', c)  constant                ('M', k)  unknown memory word at entry sp+k
# ('R', a)  return address of the call at a     None  unknown
def add(a, b):
    if a is None or b is None:
        return None
    if a[0] == "C" and b[0] == "C":
        return ("C", (a[1] + b[1]) & 0xFFFFFFFFFFFFFFFF)
    if a[0] == "S" and b[0] == "C":
        return ("S", a[1] + sgn(b[1]))
    if b[0] == "S" and a[0] == "C":
        return ("S", b[1] + sgn(a[1]))
    return None


def neg(a):
    if a and a[0] == "C":
        return ("C", (-a[1]) & 0xFFFFFFFFFFFFFFFF)
    return None


def sgn(c):
    c &= 0xFFFFFFFFFFFFFFFF
    return c - (1 << 64) if c >> 63 else c


def C(c):
    return ("C", c & 0xFFFFFFFFFFFFFFFF)


class State:
    __slots__ = ("r", "f", "m", "x")

    def __init__(self, r, f, m, x=None):
        self.r, self.f, self.m, self.x = r, f, m, x or {}

    def copy(self):
        return State(dict(self.r), dict(self.f), dict(self.m), dict(self.x))

    def key(self):
        return (tuple(sorted(self.r.items(), key=str)), tuple(sorted(self.f.items(), key=str)),
                tuple(sorted(self.m.items(), key=str)), tuple(sorted(self.x.items(), key=str)))

    def load(self, addr):
        if addr and addr[0] == "S":
            return self.m.get(addr[1], ("M", addr[1]))
        return None

    def store(self, addr, val):
        if addr and addr[0] == "S":
            self.m[addr[1]] = val


# ======================================================================= MIPS
class Mips:
    SP, RA, ZERO = 29, 31, 0
    NONVOL = list(range(16, 24)) + [30]
    FNONVOL = list(range(20, 32))
    VOLATILE = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 31]
    DELAY = True

    def entry(self):
        r = {i: ("E", i) for i in range(32)}
        r[0] = C(0)
        r[self.SP] = ("S", 0)
        return State(r, {i: ("EF", i) for i in range(32)}, {})

    @staticmethod
    def dec(w):
        return dict(op=w >> 26, rs=(w >> 21) & 31, rt=(w >> 16) & 31, rd=(w >> 11) & 31,
                    fn=w & 63, imm=sx16(w & 0xFFFF), uimm=w & 0xFFFF, w=w)

    def branch(self, w, pc):
        """Return (kind, target) for control transfers. kind: call, ret, jump,
        cond, ijump or None."""
        i = self.dec(w)
        op = i["op"]
        if op == 0 and i["fn"] == 8:
            return ("ret", None) if i["rs"] == 31 else ("ijump", None)
        if op == 0 and i["fn"] == 9:
            return ("call", None)
        if op == 2:
            return ("jump", ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
        if op == 3:
            return ("call", None)
        if op in (4, 5, 6, 7, 20, 21, 22, 23):
            if op == 4 and i["rs"] == 0 and i["rt"] == 0:
                return ("jump", pc + 4 + i["imm"] * 4)
            return ("cond", pc + 4 + i["imm"] * 4)
        if op == 1:
            if i["rt"] in (16, 17, 18, 19):
                return ("call", None)
            return ("cond", pc + 4 + i["imm"] * 4)
        if op == 17 and i["rs"] == 8:
            return ("cond", pc + 4 + i["imm"] * 4)
        return None

    def step(self, s, w):
        i = self.dec(w)
        op, rs, rt, rd, fn = i["op"], i["rs"], i["rt"], i["rd"], i["fn"]
        R = s.r
        def setr(r, v):
            if r:
                R[r] = v
        if op == 9:  # addiu
            setr(rt, add(R[rs], C(i["imm"])))
        elif op == 13:  # ori
            v = R[rs]
            setr(rt, C(v[1] | i["uimm"]) if v and v[0] == "C" else None)
        elif op == 15:  # lui
            setr(rt, C(sgn((i["uimm"] << 16) | (0xFFFFFFFF00000000 if i["uimm"] & 0x8000 else 0))))
        elif op == 0 and fn in (33, 37):  # addu / or
            if fn == 33:
                setr(rd, add(R[rs], R[rt]))
            else:
                if rt == 0:
                    setr(rd, R[rs])
                elif rs == 0:
                    setr(rd, R[rt])
                else:
                    a, b = R[rs], R[rt]
                    setr(rd, C(a[1] | b[1]) if a and b and a[0] == b[0] == "C" else None)
        elif op == 0 and fn == 35:  # subu
            setr(rd, add(R[rs], neg(R[rt])))
        elif op == 0 and fn == 36:  # and
            a, b = R[rs], R[rt]
            if a and b and a[0] == "S" and b[0] == "C":
                setr(rd, ("A", a[1], sgn(b[1])))  # realigned sp
            else:
                setr(rd, None)
        elif op == 43:  # sw
            s.store(add(R[rs], C(i["imm"])), R[rt])
        elif op == 35:  # lw
            setr(rt, s.load(add(R[rs], C(i["imm"]))))
        elif op == 57:  # swc1
            s.store(add(R[rs], C(i["imm"])), s.f.get(rt))
        elif op == 49:  # lwc1
            s.f[rt] = s.load(add(R[rs], C(i["imm"])))
        elif op == 61:  # sdc1
            a = add(R[rs], C(i["imm"]))
            s.store(a, s.f.get(rt))
            s.store(add(a, C(4)), s.f.get(rt + 1))
        elif op == 53:  # ldc1
            a = add(R[rs], C(i["imm"]))
            s.f[rt] = s.load(a)
            s.f[rt + 1] = s.load(add(a, C(4)))
        elif op in (40, 41):  # sb/sh
            pass
        elif op == 0:
            if fn in (8, 9, 12, 13, 24, 25, 26, 27):
                if fn == 9:
                    setr(rd, None)
            elif fn in (16, 18):
                setr(rd, None)
            else:
                setr(rd, None)
        elif op == 17:  # cop1
            fmt = rs
            if fmt in (0, 2):  # mfc1/cfc1
                setr(rt, None)
            elif fmt in (4, 6):
                s.f[rd] = None
            elif fmt == 8:
                pass
            else:
                fd = (w >> 6) & 31
                s.f[fd] = None
                if fmt == 17 and fn not in (32, 36, 37):
                    s.f[fd + 1] = None
                if fmt == 17 and (fn & 0x30) == 0x30:
                    pass
                if fn == 6 and fmt == 17:  # mov.d
                    s.f[fd] = s.f.get(rd); s.f[fd + 1] = s.f.get(rd + 1)
                elif fn == 6:
                    s.f[fd] = s.f.get(rd)
        elif op in (32, 33, 36, 37, 39, 34, 38):  # loads
            setr(rt, None)
        elif op in (8, 10, 11, 12, 14):
            setr(rt, None)
        elif op == 3:
            R[31] = None
        R[0] = C(0)

    def after_call(self, s, pc):
        for r in self.VOLATILE:
            s.r[r] = None
        s.r[31] = ("R", pc)
        for f in range(0, 20):
            s.f[f] = None

    def check(self, res):
        errs = []
        R, F = res["R"], res["F"]
        if R.get(29) != ("S", 0):
            errs.append("sp=%s" % (R.get(29),))
        if res["next"] != ("N", ("E", 31)):
            errs.append("next=%s" % (res["next"],))
        for r in self.NONVOL:
            if R.get(r) != ("E", r):
                errs.append("r%d=%s" % (r, R.get(r)))
        for f in self.FNONVOL:
            if F.get(f) != ("EF", f):
                errs.append("f%d=%s" % (f, F.get(f)))
        return errs


# ====================================================================== Alpha
class Alpha:
    SP, RA, ZERO = 30, 26, 31
    NONVOL = list(range(9, 16))
    FNONVOL = list(range(2, 10))
    VOLATILE = list(range(0, 9)) + list(range(16, 26)) + [27, 28]
    DELAY = False

    def entry(self):
        r = {i: ("E", i) for i in range(32)}
        r[31] = C(0)
        r[30] = ("S", 0)
        f = {i: ("EF", i) for i in range(32)}
        f[31] = C(0)
        return State(r, f, {})

    @staticmethod
    def dec(w):
        return dict(op=w >> 26, ra=(w >> 21) & 31, rb=(w >> 16) & 31, rc=w & 31,
                    disp=sx16(w & 0xFFFF), lit=(w >> 13) & 0xFF, islit=(w >> 12) & 1,
                    fn=(w >> 5) & 0x7F, ffn=(w >> 5) & 0x7FF, w=w)

    def branch(self, w, pc):
        i = self.dec(w)
        op = i["op"]
        if op == 0x1A:
            kind = (w >> 14) & 3
            if kind == 2:  # ret
                return ("ret", None)
            if kind in (1, 3):
                return ("call", None)
            return ("ijump", None)
        if op in (0x30, 0x34):
            tgt = pc + 4 + ((w & 0x1FFFFF) - (0x200000 if w & 0x100000 else 0)) * 4
            if op == 0x34 or i["ra"] != 31:
                return ("call", None) if op == 0x34 else ("jump", tgt)
            return ("jump", tgt)
        if 0x31 <= op <= 0x3F:
            tgt = pc + 4 + ((w & 0x1FFFFF) - (0x200000 if w & 0x100000 else 0)) * 4
            return ("cond", tgt)
        return None

    def step(self, s, w):
        i = self.dec(w)
        op, ra, rb, rc = i["op"], i["ra"], i["rb"], i["rc"]
        R, Fr = s.r, s.f
        def setr(r, v):
            if r != 31:
                R[r] = v
        b = C(i["lit"]) if i["islit"] else R[rb]
        if op == 0x08:  # lda
            setr(ra, add(R[rb], C(i["disp"])))
        elif op == 0x09:  # ldah
            setr(ra, add(R[rb], C(i["disp"] << 16)))
        elif op == 0x2D:  # stq
            s.store(add(R[rb], C(i["disp"])), R[ra])
        elif op == 0x2C:  # stl
            s.store(add(R[rb], C(i["disp"])), None if R[ra] != C(0) else C(0))
        elif op == 0x27:  # stt
            s.store(add(R[rb], C(i["disp"])), Fr.get(ra))
        elif op == 0x29:  # ldq
            setr(ra, s.load(add(R[rb], C(i["disp"]))))
        elif op == 0x23:  # ldt
            if ra != 31:
                Fr[ra] = s.load(add(R[rb], C(i["disp"])))
        elif op in (0x20, 0x21, 0x22):
            if ra != 31:
                Fr[ra] = None
        elif op in (0x24, 0x25, 0x26, 0x0D, 0x0E, 0x0F):
            pass
        elif op in (0x0A, 0x0B, 0x0C, 0x28, 0x2A, 0x2B, 0x2E, 0x2F):
            setr(ra, None)
        elif op == 0x10:
            if i["fn"] == 0x20:  # addq
                setr(rc, add(R[ra], b))
            elif i["fn"] == 0x29:  # subq
                setr(rc, add(R[ra], neg(b)))
            else:
                setr(rc, None)
        elif op == 0x11:
            if i["fn"] == 0x20:  # bis
                a = R[ra]
                if ra == 31:
                    setr(rc, b)
                elif not i["islit"] and rb == ra:
                    setr(rc, a)
                elif b == C(0):
                    setr(rc, a)
                elif a and b and a[0] == b[0] == "C":
                    setr(rc, C(a[1] | b[1]))
                else:
                    setr(rc, None)
            elif i["fn"] == 0x00 and b and b[0] == "C" and R[ra] and R[ra][0] == "S":  # and
                setr(rc, ("A", R[ra][1], sgn(b[1])))
            elif i["fn"] == 0x08 and b and b[0] == "C" and R[ra] and R[ra][0] == "S":  # bic
                setr(rc, ("A", R[ra][1], sgn(~b[1])))
            else:
                setr(rc, None)
        elif op in (0x12, 0x13, 0x1C):
            setr(rc, None)
        elif op == 0x17 and i["ffn"] == 0x20:  # cpys
            if rc != 31:
                Fr[rc] = Fr.get(ra) if ra == rb else None
        elif op in (0x14, 0x15, 0x16, 0x17):
            if rc != 31:
                Fr[rc] = None
        elif op == 0x1A or op in (0x30, 0x34):
            setr(ra, None)

    def after_call(self, s, pc):
        for r in self.VOLATILE:
            s.r[r] = None
        s.r[26] = ("R", pc)
        for f in [0, 1] + list(range(10, 31)):
            s.f[f] = None

    def check(self, res):
        errs = []
        R, F = res["R"], res["F"]
        if R.get(30) != ("S", 0):
            errs.append("sp=%s" % (R.get(30),))
        if res["next"] != ("N", ("E", 26)):
            errs.append("next=%s" % (res["next"],))
        for r in self.NONVOL:
            if R.get(r) != ("E", r):
                errs.append("r%d=%s" % (r, R.get(r)))
        for f in self.FNONVOL:
            if F.get(f) != ("EF", f):
                errs.append("f%d=%s" % (f, F.get(f)))
        return errs


# ==================================================================== PowerPC
class Ppc:
    SP = 1
    NONVOL = list(range(14, 32))
    FNONVOL = list(range(14, 32))
    VOLATILE = [0] + list(range(3, 13))
    DELAY = False
    RETURN = 0x4E800020
    PPC_SP_LOW = 0xFF0  # 16-byte aligned, not 32-byte aligned
    PPC_DYN_ALLOC = 0x2040

    def entry(self):
        r = {i: ("E", i) for i in range(32)}
        r[1] = ("S", 0)
        return State(r, {i: ("EF", i) for i in range(32)}, {}, {"lr": ("E", "lr"), "ctr": None})

    @staticmethod
    def dec(w):
        spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5)
        return dict(op=w >> 26, rt=(w >> 21) & 31, ra=(w >> 16) & 31, rb=(w >> 11) & 31,
                    d=sx16(w & 0xFFFF), ud=w & 0xFFFF, xo=(w >> 1) & 0x3FF, spr=spr, w=w)

    def branch(self, w, pc):
        i = self.dec(w)
        op = i["op"]
        lk = w & 1
        if op == 18:
            li = w & 0x3FFFFFC
            if li & 0x2000000:
                li -= 0x4000000
            tgt = li if w & 2 else pc + li
            return ("call", None) if lk else ("jump", tgt)
        if op == 16:
            bd = w & 0xFFFC
            if bd & 0x8000:
                bd -= 0x10000
            tgt = bd if w & 2 else pc + bd
            if lk:
                return ("call", None)
            if (i["rt"] & 0x14) == 0x14:
                return ("jump", tgt)
            return ("cond", tgt)
        if op == 19 and i["xo"] in (16, 528):
            if lk:
                return ("call", None)
            if i["xo"] == 16 and (i["rt"] & 0x14) == 0x14:
                return ("ret", None)
            if (i["rt"] & 0x14) == 0x14:
                return ("ijump", None)
            return ("cond", None)
        return None

    def step(self, s, w):
        i = self.dec(w)
        op, rt, ra, rb, xo = i["op"], i["rt"], i["ra"], i["rb"], i["xo"]
        R, X = s.r, s.x
        base = C(0) if ra == 0 else R[ra]
        if op == 14:  # addi
            R[rt] = add(base, C(i["d"]))
        elif op == 15:  # addis
            R[rt] = add(base, C(i["d"] << 16))
        elif op == 24:  # ori
            a = R[rt]
            R[ra] = C(a[1] | i["ud"]) if a and a[0] == "C" else (a if i["ud"] == 0 else None)
        elif op == 25:  # oris
            a = R[rt]
            R[ra] = C(a[1] | (i["ud"] << 16)) if a and a[0] == "C" else None
        elif op == 21 and rb == 0 and R[rt] and R[rt][0] == "S":
            # rlwinm ra,rs,0,mb,me on a stack address (stack realignment).
            # Model one concrete entry sp: its low 12 bits are PPC_SP_LOW.
            mb, me = (w >> 6) & 31, (w >> 1) & 31
            off = R[rt][1]
            low = (self.PPC_SP_LOW + off) & 0xFFF
            if mb == 0 and 20 <= me <= 31:  # clear the low 31-me bits
                R[ra] = ("S", off - (low & ((1 << (31 - me)) - 1)))
            elif me == 31 and mb >= 20:  # keep the low 32-mb bits
                R[ra] = C(low & ((1 << (32 - mb)) - 1))
            else:
                R[ra] = None
        elif op == 8:  # subfic rt = simm - ra
            R[rt] = add(C(i["d"]), neg(R[ra]))
        elif op in (26, 27, 28, 29, 20, 21, 23):  # xori,xoris,andi.,andis.,rlwimi,rlwinm,rlwnm
            R[ra] = None
        elif op == 36:  # stw
            s.store(add(base, C(i["d"])), R[rt])
        elif op == 37:  # stwu
            a = add(R[ra], C(i["d"]))
            s.store(a, R[rt])
            R[ra] = a
        elif op == 32:  # lwz
            R[rt] = s.load(add(base, C(i["d"])))
        elif op == 33:  # lwzu
            a = add(R[ra], C(i["d"]))
            R[rt] = s.load(a)
            R[ra] = a
        elif op == 54:  # stfd
            s.store(add(base, C(i["d"])), s.f.get(rt))
        elif op == 50:  # lfd
            s.f[rt] = s.load(add(base, C(i["d"])))
        elif op in (48, 49, 51):
            s.f[rt] = None
        elif op in (52, 53, 55, 38, 39, 44, 45):
            if op in (39, 45, 53, 55):
                R[ra] = None
        elif op in (34, 35, 40, 41, 42, 43, 7, 8, 12, 13):
            R[rt] = None
            if op in (35, 41, 43):
                R[ra] = None
        elif op == 31:
            if xo == 339:  # mfspr
                R[rt] = X.get("lr") if i["spr"] == 8 else (X.get("ctr") if i["spr"] == 9 else None)
            elif xo == 467:  # mtspr
                if i["spr"] == 8:
                    X["lr"] = R[rt]
                elif i["spr"] == 9:
                    X["ctr"] = R[rt]
            elif xo == 19:  # mfcr
                R[rt] = X.get("cr", ("E", "cr"))
            elif xo == 144:  # mtcrf
                if ((w >> 12) & 0xFF) == 0xFF:
                    X["cr"] = R[rt]
                else:
                    X["cr"] = None
            elif xo == 444:  # or
                if rt == rb:
                    R[ra] = R[rt]
                else:
                    R[ra] = None
            elif xo == 183:  # stwux
                if ra == 1 and R[rb] is None and R[1] and R[1][0] == "S":
                    # A dynamic allocation (alloca) of unknown size: check
                    # one representative size.
                    R[rb] = C(-self.PPC_DYN_ALLOC)
                a = add(R[ra], R[rb])
                s.store(a, R[rt])
                R[ra] = a
            elif xo == 151:  # stwx
                pass
            elif xo in (266,):  # add
                R[rt] = add(R[ra], R[rb])
            elif xo in (40, 8):  # subf, subfc rt = rb - ra
                R[rt] = add(R[rb], neg(R[ra]))
            elif xo == 104:  # neg
                R[rt] = neg(R[ra])
            elif xo in (0, 32, 4, 86, 470, 278, 54, 215, 247, 663, 727, 759, 983, 150, 151, 407, 438,
                        662, 918, 182, 214, 246):
                pass  # compares, stores, cache ops
            elif xo in (28, 60, 124, 284, 316, 412, 476, 24, 536, 792, 824, 26, 922, 954):
                R[ra] = None  # logical/shift ops write RA
            else:
                R[rt] = None
        elif op == 63 or op == 59:
            s.f[rt] = None
            if op == 63 and xo == 72:  # fmr
                s.f[rt] = s.f.get(rb)
        elif op == 18 or op == 16 or op == 19:
            pass

    def after_call(self, s, pc):
        for r in self.VOLATILE:
            s.r[r] = None
        s.x["lr"] = ("R", pc)
        s.x["ctr"] = None
        for f in range(0, 14):
            s.f[f] = None

    def check(self, res):
        errs = []
        R, F = res["R"], res["F"]
        if R.get(1) != ("S", 0):
            errs.append("sp=%s" % (R.get(1),))
        if res["next"] != ("N", ("E", "lr")):
            errs.append("next=%s" % (res["next"],))
        for r in self.NONVOL:
            if R.get(r) != ("E", r):
                errs.append("r%d=%s" % (r, R.get(r)))
        for f in self.FNONVOL:
            if F.get(f) != ("EF", f):
                errs.append("f%d=%s" % (f, F.get(f)))
        if res["X"].get("cr", ("E", "cr")) != ("E", "cr"):
            errs.append("cr=%s" % (res["X"].get("cr"),))
        return errs



# ===================================================================== NTUW
MAGIC = 0x5755544E
HEADER = 36


def s21(v):
    v &= 0x1FFFFF
    return v - (1 << 21) if v & (1 << 20) else v


def decode_program(data, first, count):
    codes = []
    slot = 0
    while slot < count:
        off = (first + slot) * 8
        code_off, info = struct.unpack_from("<II", data, off)
        op, reg, raw = info & 0x3F, (info >> 6) & 31, (info >> 11) & 0x1FFFFF
        operand = s21(raw)
        n = 1
        if 0x10 <= op <= 0x1F:
            assert raw == 0, "two-slot opcode with inline operand"
            operand = struct.unpack_from("<i", data, off + 8)[0]
            assert struct.unpack_from("<I", data, off + 12)[0] == 0
            n = 2
        elif op in (0x04, 0x07):
            operand = raw
        base = {0x10: 0x01, 0x11: 0x02, 0x12: 0x05, 0x13: 0x08}.get(op, op)
        codes.append((code_off, base, reg, operand))
        slot += n
    return codes


def decode_record(data):
    (magic, hsize, estreg, _, size, flags, prologend, estoff, pslots, nscopes,
     eslots, state, length) = struct.unpack_from("<IHBBIIIiHHHHI", data, 0)
    assert magic == MAGIC and hsize == HEADER, "bad NTUW header"
    rec = dict(flags=flags, prologend=prologend, estoff=estoff, estreg=estreg,
               length=length, size=size)
    rec["prolog"] = decode_program(data[HEADER:], 0, pslots)
    scopes_at = HEADER + pslots * 8
    codes_at = scopes_at + nscopes * 16
    rec["scopes"] = []
    for i in range(nscopes):
        start, end, first, n, _ = struct.unpack_from("<IIHHI", data, scopes_at + i * 16)
        rec["scopes"].append((start, end, decode_program(data[codes_at:], first, n)))
    return rec


class Rules:
    def __init__(self, isa):
        self.cfa = (isa.SP, 0)
        self.gpr, self.fpr, self.spr = {}, {}, {}
        self.ret = ("lr",) if isinstance(isa, Ppc) else ("gpr", isa.RA, 0)

    def copy(self):
        r = Rules.__new__(Rules)
        r.cfa, r.gpr, r.fpr, r.spr, r.ret = self.cfa, dict(self.gpr), dict(self.fpr), dict(self.spr), self.ret
        return r

    def apply(self, code):
        _, op, reg, x = code
        if op == 0x01:
            self.cfa = (reg, x)
        elif op == 0x02:
            self.gpr[reg] = ("save", x)
        elif op == 0x03:
            self.gpr.pop(reg, None)
        elif op == 0x04:
            self.gpr[reg] = ("reg", x)
        elif op == 0x05:
            self.fpr[reg] = ("save", x)
        elif op == 0x06:
            self.fpr.pop(reg, None)
        elif op == 0x07:
            self.fpr[reg] = ("reg", x)
        elif op == 0x0B:
            self.ret = ("gpr", reg, x)
        elif op == 0x16:
            self.spr[reg] = ("save", x)
        elif op == 0x17:
            self.spr[reg] = ("reg", x)
        elif op == 0x18:
            self.spr.pop(reg, None)
        else:
            raise AssertionError("opcode %#x not modelled" % op)


def evaluate(isa, rec, off):
    r = Rules(isa)
    if "parent" in rec:
        # A chained fragment starts from the parent's body state.
        for c in rec["parent"]["prolog"]:
            r.apply(c)
    inprolog = off < rec["prologend"]
    for c in rec["prolog"]:
        if not inprolog or c[0] <= off:
            r.apply(c)
    inscope = False
    if not inprolog:
        for start, end, codes in rec["scopes"]:
            if start <= off < end:
                inscope = True
                for c in codes:
                    if c[0] <= off - start:
                        r.apply(c)
                break
    return r, inprolog, inscope


def unwind(isa, rec, off, st):
    r, inprolog, inscope = evaluate(isa, rec, off)
    is_mips = isinstance(isa, Mips)
    cfa = add(st.r.get(r.cfa[0]), C(r.cfa[1]))
    R, F = dict(st.r), dict(st.f)
    X = dict(st.x)
    for g, (kind, x) in r.gpr.items():
        R[g] = st.load(add(cfa, C(x))) if kind == "save" else st.r.get(x)
    for f, (kind, x) in r.fpr.items():
        if kind == "save":
            F[f] = st.load(add(cfa, C(x)))
            if is_mips:
                F[f + 1] = st.load(add(cfa, C(x + 4)))
        else:
            F[f] = st.f.get(x)
            if is_mips:
                F[f + 1] = st.f.get(x + 1)
    names = {0: "lr", 1: "cr", 2: "ctr"}
    for s, (kind, x) in r.spr.items():
        X[names[s]] = st.load(add(cfa, C(x))) if kind == "save" else st.r.get(x)
    R[isa.SP] = cfa
    if r.ret[0] == "lr":
        nxt = ("N", X.get("lr"))
    else:
        nxt = ("N", R.get(r.ret[1]))
    infunc = not inprolog and not inscope
    est = None
    owner = rec.get("parent", rec)
    if infunc:
        if owner["estreg"]:
            est = add(st.r.get(owner["estreg"]), C(owner["estoff"]))
        else:
            est = add(cfa, C(owner["estoff"]))
    return dict(R=R, F=F, X=X, next=nxt, est=est, infunc=infunc)


def exit_errors(isa, st):
    """At a return or tail call the state must already be the caller's."""
    if isinstance(isa, Ppc):
        ret = st.x.get("lr")
    else:
        ret = st.r.get(isa.RA)
    return isa.check(dict(R=st.r, F=st.f, X=st.x, next=("N", ret)))


def in_function(fe, pc):
    return any(r["begin"] <= pc < r["end"] for r in fe["ranges"])


def fragment_at(fe, pc):
    for r in fe["ranges"]:
        if r["begin"] <= pc < r["end"]:
            return r
    return None


def jump_target(co, fe, pc, w, tgt):
    """Resolve an unconditional branch: (target, is_tail_call). A relocated
    branch to a symbol outside the function is a tail call; MIPS also
    relocates jumps within a function, against the section symbol."""
    relocs = co.secs[fe["sec"]]["relocs"].get(pc)
    if not relocs:
        return tgt, False
    sym = co.syms[relocs[0][0]]
    if sym["sec"] - 1 != fe["sec"]:
        return None, True
    if co.machine == MACH_MIPS:
        addend = (w & 0x3FFFFFF) << 2
    elif co.machine == MACH_PPC:
        addend = w & 0x3FFFFFC
        addend -= 0x4000000 if addend & 0x2000000 else 0
        addend = 0 if not w & 2 else addend
    else:
        addend = 0
    tgt = sym["value"] + addend
    # Function symbols are referenced by name; a section-relative branch
    # targets a basic block, and one outside the function is the dead branch
    # that follows a noreturn call.
    return tgt, not sym["name"].startswith(".") and not (
        in_function(fe, tgt))


def analyze(path, only=(), verbose=False):
    co = Coff(path)
    isa = {MACH_ALPHA: Alpha, MACH_ALPHA64: Alpha, MACH_MIPS: Mips,
           MACH_PPC: Ppc}.get(co.machine)
    if isa is None:
        print("%s: unsupported machine %#x" % (path, co.machine))
        return 1
    isa = isa()
    if not any(s and s["name"] == "@unwind.00" for s in co.syms):
        print("%s: not an NTUW object" % path)
        return 1
    funcs = []
    for si, s in enumerate(co.secs):
        if not s["name"].startswith(".pdata"):
            continue
        for off in range(0, len(s["data"]) - 7, 8):
            b, _ = co.word_at(si, off)
            x, raw = co.word_at(si, off + 4)
            if b and not x and isinstance(raw, int) and raw & 1:
                # Packed frameless entry: the entry state everywhere.
                assert (raw >> 1) & 3 == 0, "reserved packed kind"
                rec = dict(flags=0, prologend=0, estoff=0, estreg=0,
                           length=(raw >> 3) * 4, size=0, prolog=[], scopes=[])
                funcs.append(dict(sec=b[0], begin=b[1], end=b[1] + rec["length"],
                                  rec=rec, name=co.func_name(*b), parent=None))
                continue
            if not b or not x:
                continue
            rec = decode_record(co.secs[x[0]]["data"][x[1]:])
            fe = dict(sec=b[0], begin=b[1], end=b[1] + rec["length"], rec=rec,
                      name=co.func_name(*b), parent=None)
            if rec["flags"] & 4:
                # The tail is the parent's .pdata entry.
                pb, _ = co.word_at(x[0], x[1] + rec["size"] - 8)
                px, _ = co.word_at(x[0], x[1] + rec["size"] - 4)
                fe["parent"] = pb
                rec["parent"] = decode_record(co.secs[px[0]]["data"][px[1]:])
            funcs.append(fe)
    # A function's chained fragments share its walk.
    for fe in funcs:
        fe["ranges"] = [fe]
    for fe in funcs:
        if fe["parent"]:
            for p in funcs:
                if (p["sec"], p["begin"]) == fe["parent"]:
                    p["ranges"].append(fe)
    total_pcs = bad = 0
    for fe in funcs:
        if fe["parent"] or (only and fe["name"] not in only):
            continue
        rec = fe["rec"]
        sec = co.secs[fe["sec"]]
        code = sec["data"]

        def word(a, code=code):
            if a < 0 or a + 4 > len(code):
                return 0
            return struct.unpack_from("<I", code, a)[0]

        work = [(fe["begin"], isa.entry())]
        seen, problems, ests = {}, {}, {}

        def check(pc, st):
            frag = fragment_at(fe, pc)
            res = unwind(isa, frag["rec"], pc - frag["begin"], st)
            errs = isa.check(res)
            if res["infunc"]:
                ests.setdefault(res["est"], pc)
            if errs:
                problems.setdefault(pc, []).extend(errs)

        def leave(pc, st, what):
            errs = exit_errors(isa, st)
            if errs:
                problems.setdefault(pc, []).extend(
                    "at %s: %s" % (what, e) for e in errs)

        while work:
            pc, st = work.pop()
            if not in_function(fe, pc):
                continue
            k = st.key()
            lst = seen.setdefault(pc, [])
            if k in lst or len(lst) > 6:
                continue
            lst.append(k)
            w = word(pc)
            check(pc, st)
            total_pcs += 1
            br = isa.branch(w, pc)
            nst = st.copy()
            isa.step(nst, w)
            if br is None:
                work.append((pc + 4, nst))
                continue
            kind, tgt = br
            nxt = pc + 4
            if isa.DELAY:
                # A fault in a delay slot reports the branch, so the delay
                # slot is never itself a ControlPc; the state after it is
                # checked at the branch target or the return address.
                isa.step(nst, word(pc + 4))
                nxt = pc + 8
            if kind == "call":
                isa.after_call(nst, pc)
                work.append((nxt, nst))
            elif kind == "ret":
                leave(pc, nst, "return")
            elif kind == "jump":
                tgt, tail = jump_target(co, fe, pc, w, tgt)
                if tail:
                    leave(pc, nst, "tail call")
                elif tgt is not None and in_function(fe, tgt):
                    work.append((tgt, nst))
                # Otherwise the branch leaves the function without a
                # relocation: unreachable code after a noreturn call.
            elif kind == "cond":
                if tgt is not None:
                    work.append((tgt, nst.copy()))
                work.append((nxt, nst))
        bad_est = [e for e in ests if not (e and e[0] == "S")]
        if bad_est or len(ests) > 1:
            for e, pc in ests.items():
                problems.setdefault(pc, []).append("establisher=%s" % (e,))
        est_note = ""
        if len(ests) == 1 and not bad_est:
            est_note = " est=CFA%+d" % next(iter(ests))[1]
        status = ("ok" if not problems else "%d bad PCs" % len(problems)) + est_note
        if problems or verbose:
            chained = len(fe["ranges"]) - 1
            print("%s [%#x-%#x) prologend=%#x scopes=%d%s: %s" % (
                fe["name"], fe["begin"], max(r["end"] for r in fe["ranges"]),
                rec["prologend"], sum(len(r["rec"]["scopes"]) for r in fe["ranges"]),
                " chained=%d" % chained if chained else "", status))
        for pc in sorted(problems):
            print("   +%#x %08x: %s" % (pc - fe["begin"], word(pc),
                                         "; ".join(problems[pc])))
        bad += bool(problems)
    print("%s: %d functions, %d bad, %d PCs checked" % (
        path, len(funcs), bad, total_pcs))
    return bad


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    if not args:
        sys.exit(__doc__)
    sys.exit(1 if analyze(args[0], args[1:], "-v" in sys.argv) else 0)
