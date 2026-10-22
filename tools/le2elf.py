"""le2elf.py: turn a DOS/4GW LE executable or an LX driver into an ELF file.

Ghidra has no loader for the Linear Executable formats, LE (LBA.EXE,
INSTALL.EXE) and LX (the CD's *.DLL drivers), but loads ELF. The objects
are laid at their own base addresses and every internal fixup is applied
there, so the ELF is the image DOS/4GW would have built at those bases.
The ELF carries no relocations (Ghidra would apply them a second time).
The fixups go to a sidecar file instead, one site a line:

    va  kind  size  tgtobj  tgtoff  tgtva  page

kind is off32, rel32, off16, sel16, ptr32, ptr48 or byte. A selector
fixup (the DGROUP loads in the interrupt handlers) is left zero and
listed as sel16.

    le2elf.py [-v] [-x obj]... [-s map] [-f fix] -o out.elf in.exe

-v prints the header, the objects and a histogram of the fixup records.
-x marks an object executable (gusmid32.dll's code object is not).
-s adds a symbol table from a map, a line per symbol:

    addr type name [size]

addr is 0xVA or obj:offset (as Watcom maps print it, 0001:00012345),
type T for code, D or B for data (an object of size bytes), U for a
label (an unreachable block). A map may also cut an object into
regions, lines of type X (code), R (read-only data) or W (variables)
with their sizes, which must cover the object exactly: each region is
then a section of its own, with those permissions, so that Ghidra
decompiles variables as variables and tables as tables. The segments
(and so the bytes the oracles map) stay one per object.
"""
import collections
import hashlib
import os
import struct
import sys


def fail(msg):
    sys.stderr.write("%s: %s\n" % (os.path.basename(sys.argv[0]), msg))
    sys.exit(1)


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def s16(b, o):
    return struct.unpack_from("<h", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class Obj:
    """One object: its base, size, flags and the bytes the file gives it."""

    def __init__(self, n, vsize, base, flags, pageidx, npages):
        self.n = n
        self.vsize = vsize
        self.base = base
        self.flags = flags
        self.pageidx = pageidx
        self.npages = npages
        self.data = bytearray()

    def kind(self):
        if self.flags & 4:
            return "text"
        if self.flags & 2:
            return "data"
        return "rodata"


class Linear:
    """A parsed LE or LX file."""

    def __init__(self, path):
        b = open(path, "rb").read()
        self.b = b
        self.sha = hashlib.sha256(b).hexdigest()
        if b[:2] != b"MZ":
            fail("%s: not an MZ executable" % path)
        h = u32(b, 0x3c)
        self.sig = b[h:h+2].decode("latin-1")
        if self.sig not in ("LE", "LX") or b[h+2] != 0 or b[h+3] != 0:
            fail("%s: not a little-endian LE or LX image" % path)
        self.npages = u32(b, h+0x14)
        self.eipobj = u32(b, h+0x18)
        self.eip = u32(b, h+0x1c)
        self.espobj = u32(b, h+0x20)
        self.esp = u32(b, h+0x24)
        self.pagesize = u32(b, h+0x28)
        self.lastpage = u32(b, h+0x2c)
        objtab = h + u32(b, h+0x40)
        nobj = u32(b, h+0x44)
        self.pagemap = h + u32(b, h+0x48)
        self.fixpages = h + u32(b, h+0x68)
        self.fixrecs = h + u32(b, h+0x6c)
        self.datapages = u32(b, h+0x80)
        self.objs = []
        for i in range(nobj):
            vsize, base, flags, pageidx, npages = struct.unpack_from("<5I", b, objtab+24*i)
            self.objs.append(Obj(i+1, vsize, base, flags, pageidx, npages))
        self.pagetypes = collections.Counter()
        for o in self.objs:
            self.load(o)

    def page(self, p):
        """File offset, size and type of module page p, counted from 1."""
        b = self.b
        if self.sig == "LE":
            e = self.pagemap + 4*(p-1)
            num = b[e]<<16 | b[e+1]<<8 | b[e+2]
            typ = b[e+3]
            size = self.pagesize
            if p == self.npages:
                size = self.lastpage
            return self.datapages + (num-1)*self.pagesize, size, typ
        e = self.pagemap + 8*(p-1)
        off, size, typ = struct.unpack_from("<IHH", b, e)
        return self.datapages + (off << self.lastpage), size, typ

    def load(self, o):
        for i in range(o.npages):
            off, size, typ = self.page(o.pageidx + i)
            self.pagetypes[typ] += 1
            if typ == 3:
                chunk = bytes(self.pagesize)
            elif typ != 0:
                fail("object %d page %d: page type %d not handled" % (o.n, o.pageidx+i, typ))
            else:
                chunk = self.b[off:off+size]
            if len(o.data) < i*self.pagesize:
                o.data += bytes(i*self.pagesize - len(o.data))
            o.data[i*self.pagesize:] = chunk
        if len(o.data) > o.vsize:
            del o.data[o.vsize:]

    def obj(self, n):
        if n < 1 or n > len(self.objs):
            fail("fixup to object %d of %d" % (n, len(self.objs)))
        return self.objs[n-1]

    def fixups(self):
        """Yield one dictionary per fixup site, in file order."""
        b = self.b
        for o in self.objs:
            for i in range(o.npages):
                p = o.pageidx + i
                start = self.fixrecs + u32(b, self.fixpages + 4*(p-1))
                end = self.fixrecs + u32(b, self.fixpages + 4*p)
                r = start
                while r < end:
                    r = self.record(o, i, r, end)

    def record(self, o, pageno, r, end):
        b = self.b
        src = b[r]
        flags = b[r+1]
        r += 2
        stype = src & 0xf
        if src & 0x20:
            count = b[r]
            r += 1
            srcoffs = None
        else:
            srcoffs = [s16(b, r)]
            r += 2
        ttype = flags & 3
        tgtobj = 0
        tgtoff = 0
        if ttype == 0:
            if flags & 0x40:
                tgtobj = u16(b, r)
                r += 2
            else:
                tgtobj = b[r]
                r += 1
            if stype != 2:
                if flags & 0x10:
                    tgtoff = u32(b, r)
                    r += 4
                else:
                    tgtoff = u16(b, r)
                    r += 2
        else:
            fail("object %d page %d: import or entry fixup (target type %d) not handled" % (o.n, o.pageidx+pageno, ttype))
        if flags & 4:
            if flags & 0x20:
                tgtoff += u32(b, r)
                r += 4
            else:
                tgtoff += u16(b, r)
                r += 2
        if srcoffs is None:
            srcoffs = [s16(b, r+2*k) for k in range(count)]
            r += 2*count
        self.histogram[(src, flags)] += len(srcoffs)
        for so in srcoffs:
            self.sites.append(dict(obj=o, page=o.pageidx+pageno, va=o.base + pageno*self.pagesize + so,
                stype=stype, tgtobj=tgtobj, tgtoff=tgtoff))
        if r > end:
            fail("object %d: fixup record runs past its page" % o.n)
        return r

    def apply(self):
        """Apply every internal fixup and return the deduplicated sites."""
        self.histogram = collections.Counter()
        self.sites = []
        self.fixups()
        done = {}
        self.split = 0
        for s in self.sites:
            o = s["obj"]
            t = self.obj(s["tgtobj"])
            tgtva = (t.base + s["tgtoff"]) & 0xffffffff
            kind, size, value = {
                0x0: ("byte", 1, tgtva & 0xff),
                0x2: ("sel16", 2, 0),
                0x3: ("ptr32", 4, tgtva & 0xffff),
                0x5: ("off16", 2, tgtva & 0xffff),
                0x6: ("ptr48", 6, tgtva),
                0x7: ("off32", 4, tgtva),
                0x8: ("rel32", 4, (tgtva - (s["va"]+4)) & 0xffffffff),
            }.get(s["stype"], (None, 0, 0))
            if kind is None:
                fail("fixup source type %#x at %#x not handled" % (s["stype"], s["va"]))
            s.update(kind=kind, size=size, tgtva=tgtva)
            off = s["va"] - o.base
            if s["va"] in done:
                d = done[s["va"]]
                if (d["kind"], d["tgtva"]) != (kind, tgtva):
                    fail("fixups at %#x disagree" % s["va"])
                self.split += 1
                continue
            done[s["va"]] = s
            if off < 0 or off + size > len(o.data):
                if off < 0 or off >= o.vsize:
                    fail("fixup at %#x outside object %d" % (s["va"], o.n))
                o.data += bytes(off + size - len(o.data))
            if size == 6:
                o.data[off:off+4] = struct.pack("<I", value)
                o.data[off+4:off+6] = b"\0\0"
            elif size == 4:
                o.data[off:off+4] = struct.pack("<I", value)
            elif size == 2:
                o.data[off:off+2] = struct.pack("<H", value)
            else:
                o.data[off] = value
        return sorted(done.values(), key=lambda s: s["va"])

    def entry(self):
        if self.eipobj == 0:
            return self.objs[0].base
        return self.obj(self.eipobj).base + self.eip


def readmap(path, lin):
    """Symbols from a map: (va, type, name, size)."""
    syms = []
    for line in open(path, encoding="latin-1"):
        f = line.split()
        if not f or f[0].startswith("#"):
            continue
        if len(f) < 3:
            fail("%s: short line: %s" % (path, line.rstrip()))
        a = f[0]
        if ":" in a:
            n, off = a.split(":")
            va = lin.obj(int(n, 16)).base + int(off, 16)
        else:
            va = int(a, 16)
        size = 0
        if len(f) > 3:
            size = int(f[3], 0)
        if f[1] not in ("T", "D", "B", "U", "X", "R", "W"):
            fail("%s: type %s: %s" % (path, f[1], line.rstrip()))
        syms.append((va, f[1], f[2], size))
    return syms


def regions(lin, syms):
    """The regions (X, R, W lines) cutting each object, checked to cover it."""
    regs = {}
    for va, typ, name, size in sorted(syms):
        if typ not in ("X", "R", "W"):
            continue
        o = None
        for x in lin.objs:
            if x.base <= va < x.base + len(x.data):
                o = x
        if o is None or size <= 0 or va + size > o.base + len(o.data):
            fail("region %s at %#x: outside an object's bytes" % (name, va))
        regs.setdefault(o.n, []).append((va, typ, name, size))
    for n, rs in regs.items():
        o = lin.obj(n)
        at = o.base
        for va, typ, name, size in rs:
            if va != at:
                fail("object %d: region %s at %#x, expected %#x" % (n, name, va, at))
            at += size
        if at != o.base + len(o.data):
            fail("object %d: regions end at %#x, its bytes at %#x" % (n, at, o.base + len(o.data)))
    return regs


class Strtab:
    def __init__(self):
        self.b = bytearray(b"\0")
        self.at = {"": 0}

    def add(self, s):
        if s not in self.at:
            self.at[s] = len(self.b)
            self.b += s.encode("latin-1") + b"\0"
        return self.at[s]


def writeelf(path, lin, syms, execobjs):
    objs = [o for o in lin.objs if o.vsize > 0]
    nph = len(objs)
    off = 52 + 32*nph
    loads = []
    for o in objs:
        flags = 4
        if o.flags & 2:
            flags |= 2
        if o.flags & 4 or o.n in execobjs:
            flags |= 1
        off += (o.base - off) % 0x1000
        loads.append((o, off, flags))
        off += len(o.data)
    body = bytearray(off)
    for o, at, _ in loads:
        body[at:at+len(o.data)] = o.data

    shstr = Strtab()
    sections = []    # name, type, flags, addr, offset, size, link, info, align, entsize
    counts = collections.Counter()
    secidx = {}
    regs = regions(lin, syms)
    for o, at, flags in loads:
        k = o.kind()
        if o.n in execobjs:
            k = "text"
        name = "." + k
        counts[k] += 1
        if counts[k] > 1:
            name = ".o%d%s" % (o.n, k)
        shflags = 2 | (1 if flags & 2 else 0) | (4 if flags & 1 else 0)
        secidx[o.n] = []
        if o.n in regs:
            for va, typ, rname, size in regs[o.n]:
                rflags = 2 | {"X": 4, "R": 0, "W": 1}[typ]
                secidx[o.n].append((va, va + size, len(sections) + 1))
                sections.append(["." + rname, 1, rflags, va, at + va - o.base, size, 0, 0, 1, 0])
        else:
            secidx[o.n].append((o.base, o.base + len(o.data), len(sections) + 1))
            sections.append([name, 1, shflags, o.base, at, len(o.data), 0, 0, 16, 0])
        if o.vsize > len(o.data):
            secidx[o.n].append((o.base + len(o.data), o.base + o.vsize, len(sections) + 1))
            sections.append([".o%dbss" % o.n, 8, 3, o.base + len(o.data), at + len(o.data), o.vsize - len(o.data), 0, 0, 16, 0])

    comment = ("le2elf %s sha256 %s\n" % (os.path.basename(lin.path), lin.sha)).encode() + b"\0"
    tail = bytearray()

    def append(data, align):
        nonlocal tail
        pad = (-(len(body) + len(tail))) % align
        tail += bytes(pad)
        at = len(body) + len(tail)
        tail += data
        return at

    at = append(comment, 1)
    sections.append([".comment", 1, 0, 0, at, len(comment), 0, 0, 1, 0])
    if syms:
        strtab = Strtab()
        symtab = bytearray(16)
        nlocal = sum(1 for x in syms if x[1] == "U")
        for va, typ, name, size in sorted(syms, key=lambda x: (x[1] != "U", x[0], x[2])):
            if typ in ("X", "R", "W"):
                continue
            shndx = 0xfff1
            for o in lin.objs:
                for lo, hi, i in secidx.get(o.n, []):
                    if lo <= va < hi:
                        shndx = i
            bind, st = 0x10, {"T": 2, "D": 1, "B": 1, "U": 0}[typ]
            if typ == "U":
                bind, size = 0, 0
            symtab += struct.pack("<IIIBBH", strtab.add(name), va, size, bind | st, 0, shndx)
        nsec = len(sections)
        at = append(bytes(symtab), 4)
        sections.append([".symtab", 2, 0, 0, at, len(symtab), nsec + 2, 1 + nlocal, 4, 16])
        at = append(bytes(strtab.b), 1)
        sections.append([".strtab", 3, 0, 0, at, len(strtab.b), 0, 0, 1, 0])
    for s in sections:
        shstr.add(s[0])
    shstr.add(".shstrtab")
    at = append(bytes(shstr.b), 1)
    sections.append([".shstrtab", 3, 0, 0, at, len(shstr.b), 0, 0, 1, 0])
    pad = (-(len(body) + len(tail))) % 4
    tail += bytes(pad)
    shoff = len(body) + len(tail)
    sh = bytearray(40)
    for name, typ, flags, addr, offset, size, link, info, align, entsize in sections:
        sh += struct.pack("<10I", shstr.add(name), typ, flags, addr, offset, size, link, info, align, entsize)

    ident = b"\x7fELF" + bytes([1, 1, 1, 0]) + bytes(8)
    hdr = ident + struct.pack("<HHIIIIIHHHHHH", 2, 3, 1, lin.entry(), 52, shoff, 0, 52, 32, nph, 40,
        len(sections) + 1, len(sections))
    body[0:52] = hdr
    for i, (o, at, flags) in enumerate(loads):
        body[52+32*i:52+32*(i+1)] = struct.pack("<8I", 1, at, o.base, o.base, len(o.data), o.vsize, flags, 0x1000)
    with open(path, "wb") as f:
        f.write(body)
        f.write(tail)
        f.write(sh)


def report(lin, sites):
    w = sys.stderr.write
    w("%s: %s, %d pages of %d, entry %d:%#x (%#x), stack %d:%#x\n" % (lin.path, lin.sig, lin.npages,
        lin.pagesize, lin.eipobj, lin.eip, lin.entry(), lin.espobj, lin.esp))
    for o in lin.objs:
        w("  object %d: base %#x size %#x flags %#x pages %d..%d, %#x bytes in the file\n" % (o.n, o.base,
            o.vsize, o.flags, o.pageidx, o.pageidx + o.npages - 1, len(o.data)))
    w("  page types: %s\n" % " ".join("%d=%d" % kv for kv in sorted(lin.pagetypes.items())))
    total = sum(lin.histogram.values())
    hist = " ".join("%02x/%02x=%d" % (k[0], k[1], v) for k, v in sorted(lin.histogram.items()))
    w("  fixups %d %s split=%d, %d sites\n" % (total, hist, lin.split, len(sites)))
    for s in sites:
        if s["kind"] == "sel16":
            w("  selector at %#x to object %d\n" % (s["va"], s["tgtobj"]))


def main(args):
    verbose = False
    execobjs = set()
    mapfile = None
    fixfile = None
    out = None
    while args and args[0].startswith("-"):
        a = args.pop(0)
        if a == "-v":
            verbose = True
        elif a == "-x" and args:
            execobjs.add(int(args.pop(0)))
        elif a == "-s" and args:
            mapfile = args.pop(0)
        elif a == "-f" and args:
            fixfile = args.pop(0)
        elif a == "-o" and args:
            out = args.pop(0)
        else:
            fail("usage: le2elf.py [-v] [-x obj]... [-s map] [-f fix] -o out.elf in.exe")
    if len(args) != 1 or (out is None and not verbose):
        fail("usage: le2elf.py [-v] [-x obj]... [-s map] [-f fix] -o out.elf in.exe")
    lin = Linear(args[0])
    lin.path = args[0]
    sites = lin.apply()
    if verbose:
        report(lin, sites)
    if fixfile:
        with open(fixfile, "w") as f:
            for s in sites:
                f.write("%#x\t%s\t%d\t%d\t%#x\t%#x\t%d\n" % (s["va"], s["kind"], s["size"], s["tgtobj"],
                    s["tgtoff"], s["tgtva"], s["page"]))
    if out:
        syms = readmap(mapfile, lin) if mapfile else []
        writeelf(out, lin, syms, execobjs)


if __name__ == "__main__":
    main(sys.argv[1:])
