"""lecmp.py: compare the functions of two images, each one that le2elf.py
made or a PE (LbaWin.exe).

Two builds of the same code differ where the linker put things: in the
targets of fixups (absolute addresses) and in the displacements of calls
and jumps between functions, which LE images keep relative and without a
fixup. Both are masked: a fixup site matches a fixup site of the same size,
and the displacement of a call or jump (E8, E9, or 0F 80 to 0F 8F, a
conditional jump) present in both images matches whatever it holds. The
calls and jumps are those of a's instructions, decoded from the start of
each function, a fixup site where an instruction would start being data
(a jump table): an E8 inside another instruction (the D1 E8 of shr eax,1)
counts as that instruction's. An image whose fixups are not known, a PE
without base relocations as LbaWin's is, says nothing of where its
addresses are: there a 32-bit fixup site of the other image matches four
bytes that hold an address within the image, and the two addresses make a
pair, a's target and b's (-l). Assemblers also differ in the no-ops they
pad aligned code with and in the direction they encode an operation
between two registers. Those count as equivalent, a run of no-ops matching
any run of no-ops of the same length (MASM 6.1 pads with 8B C0, 2E 8B C0
and 8D 40 00, MASM 6.14 with 8B FF and 8D 49 00, JWasm with 8B FF and
8D 40 00), and a function equal only through them is reported as equiv
rather than ident. Everything else must be equal.

    lecmp.py a a.sym b b.sym                      compare the functions named in both
    lecmp.py -a [-m n] [-d] [-l log] a a.sym b    find each function of a in b's code
    lecmp.py -s a a.sym                           the sums of a's functions and data

The images are ELF files from le2elf.py, their fixup sites in the .fix
beside each, or PE files, their sites from the base relocations when they
have them. The maps are those le2elf.py -s reads, at flat addresses (addr
type name [size]), columns past the size ignored. Each line printed is

    name  a_va  b_va  size  verdict  differing_bytes  first_difference

with verdict ident, equiv, pad or diff (pad: the same bytes once each is
trimmed of its padding as for -a, the map's sizes apart, as when the
next function moved), or for -a

    name  a_va  matches  size

the address of each match in b (one when the function is found once,
several when it is not unique, - when it is absent). A size taken from a
map runs to the next symbol, so the last function of a module carries the
padding put after it: fill bytes (zero, nop, int3) at the end, and the
no-ops after a function's last return, are left out of a search. -m n
leaves out the functions shorter than n bytes (16). With -d a function not
found gets a fifth column, addr:n, the place in b's code where it comes
nearest and the bytes that differ there: its runs of 8 bytes vote for the
places in b holding them, and the best voted are compared. With -l, each
fixup site of a function found once where the images' fixups part, one
image's unknown, is written to log as a line

    a_target  b_value  a_site  b_site  name

the pairs from which the data of a can be named in b: a target given two
values is a match to look at, or a structure laid out differently.

With -s, each symbol of a's map is a line, sorted by name,

    name  size  crc

its size (a function's trimmed of its padding, as for -a) and the CRC-32
of its bytes, each fixup site and each call or jump to another
function zeroed, then of what they point at: the symbol a fixup's
target starts, or its offset when it lies in the same function (a jump
table), or else its kind, and for a call or a jump the function it
lands in, with the offset into it when it lands past the start (a
label of the assembly the map leaves unnamed). A line
changes when its code, its value or a name it refers to does, an offset
within a datum aside, whatever moved around it, so that the sums of two
builds compare without the images. A size runs to
the next symbol of the map, so that a module's private data counts in
the symbol before it, and a function's table of initial values (Watcom
puts them ahead of a module's first function) in the function before.
"""
import bisect
import collections
import os
import struct
import sys
import zlib


def fail(msg):
    sys.stderr.write("%s: %s\n" % (os.path.basename(sys.argv[0]), msg))
    sys.exit(1)


class Image:
    """The loaded segments of an ELF or a PE, its fixup sites and its code
    ranges."""

    def __init__(self, path):
        b = open(path, "rb").read()
        self.segs = []
        self.fix = {}
        self.fixat = {}
        self.fixkind = {}
        self.hasfix = False
        self.grams = None
        self.decoded = {}
        if b[:4] == b"\x7fELF":
            self.elf(path, b)
        elif b[:2] == b"MZ":
            self.pe(path, b)
        else:
            fail("%s: not an ELF or a PE file" % path)
        if not self.segs:
            fail("%s: nothing loaded" % path)
        self.lo = min(base for base, data, x in self.segs)
        self.hi = max(base + len(data) for base, data, x in self.segs)

    def addfix(self, va, size, kind="off32"):
        self.fixat[va] = size
        self.fixkind[va] = kind
        for k in range(size):
            self.fix[va + k] = size

    def elf(self, path, b):
        phoff, = struct.unpack_from("<I", b, 0x1c)
        phnum, = struct.unpack_from("<H", b, 0x2c)
        for i in range(phnum):
            typ, off, va, _, filesz, memsz, flags, _ = struct.unpack_from("<8I", b, phoff + 32*i)
            if typ == 1:
                data = bytearray(b[off:off+filesz]) + bytearray(memsz - filesz)
                self.segs.append((va, data, flags & 1))
        fixpath = os.path.splitext(path)[0] + ".fix"
        if os.path.exists(fixpath):
            self.hasfix = True
            for line in open(fixpath):
                f = line.split("\t")
                self.addfix(int(f[0], 16), int(f[2]), f[1])

    def pe(self, path, b):
        pe, = struct.unpack_from("<I", b, 0x3c)
        if b[pe:pe+4] != b"PE\0\0":
            fail("%s: not a PE file" % path)
        nsect, = struct.unpack_from("<H", b, pe + 6)
        optsize, = struct.unpack_from("<H", b, pe + 20)
        opt = pe + 24
        if struct.unpack_from("<H", b, opt)[0] != 0x10b:
            fail("%s: not a 32-bit PE" % path)
        base, = struct.unpack_from("<I", b, opt + 28)
        ndirs, = struct.unpack_from("<I", b, opt + 92)
        for i in range(nsect):
            s = opt + optsize + 40*i
            vsize, rva, rawsize, rawoff = struct.unpack_from("<IIII", b, s + 8)
            chars, = struct.unpack_from("<I", b, s + 36)
            size = vsize or rawsize
            data = bytearray(b[rawoff:rawoff+min(size, rawsize)])
            data += bytearray(size - len(data))
            self.segs.append((base + rva, data, 1 if chars & 0x20000000 else 0))
        if ndirs > 5:
            rva, size = struct.unpack_from("<II", b, opt + 96 + 8*5)
            rel = self.read(base + rva, size) if size else None
            if rel:
                self.hasfix = True
                p = 0
                while p + 8 <= len(rel):
                    page, n = struct.unpack_from("<II", rel, p)
                    if n < 8:
                        break
                    for k in range(8, n, 2):
                        e, = struct.unpack_from("<H", rel, p + k)
                        if e >> 12 == 3:
                            self.addfix(base + page + (e & 0xfff), 4)
                    p += n

    def seg(self, va):
        for base, data, x in self.segs:
            if base <= va < base + len(data):
                return base, data
        return None, None

    def read(self, va, n):
        base, data = self.seg(va)
        if base is None:
            return None
        return bytes(data[va-base:va-base+n])

    def code(self):
        return [(base, data) for base, data, x in self.segs if x]

    def inside(self, v):
        return self.lo <= v < self.hi

    def index(self):
        """Every run of 8 bytes of the code, with the places it is at."""
        if self.grams is None:
            self.grams = collections.defaultdict(list)
            for base, data in self.code():
                for p in range(len(data) - 7):
                    self.grams[bytes(data[p:p+8])].append(base + p)
        return self.grams


def readsyms(path):
    syms = []
    for line in open(path, encoding="latin-1"):
        f = line.split()
        if len(f) < 3 or f[0].startswith("#"):
            continue
        size = int(f[3], 0) if len(f) > 3 else 0
        syms.append((int(f[0], 16), f[1], f[2], size))
    return syms


# The opcodes of the integer instructions in 32-bit code, the 386's and
# those its successors added up to the Pentium Pro, one byte (ONE) or
# after 0F (TWO): whether a ModRM byte follows, and the size of the
# immediate after it, a count of bytes (0, 1, 2), or OPS a word or a
# doubleword as the operand size says (a relative call or jump's
# displacement among them), PTR a far pointer, MOFFS an offset of the
# address size, ENTER a word and a byte.
OPS, PTR, MOFFS, ENTER = -1, -2, -3, -4
PREFIXES = {0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67, 0xf0, 0xf2, 0xf3}
ONE = {}
for op in range(0x00, 0x40, 8):
    for k in range(4):
        ONE[op + k] = (True, 0)
    ONE[op + 4] = (False, 1)
    ONE[op + 5] = (False, OPS)
for op in (0x06, 0x07, 0x0e, 0x16, 0x17, 0x1e, 0x1f, 0x27, 0x2f, 0x37, 0x3f):
    ONE[op] = (False, 0)
for op in range(0x40, 0x62):
    ONE[op] = (False, 0)
ONE[0x62] = ONE[0x63] = (True, 0)
ONE[0x68] = (False, OPS)
ONE[0x69] = (True, OPS)
ONE[0x6a] = (False, 1)
ONE[0x6b] = (True, 1)
for op in range(0x6c, 0x70):
    ONE[op] = (False, 0)
for op in range(0x70, 0x80):
    ONE[op] = (False, 1)
ONE[0x80] = ONE[0x82] = ONE[0x83] = (True, 1)
ONE[0x81] = (True, OPS)
for op in range(0x84, 0x90):
    ONE[op] = (True, 0)
for op in range(0x90, 0xa0):
    ONE[op] = (False, 0)
ONE[0x9a] = (False, PTR)
for op in range(0xa0, 0xa4):
    ONE[op] = (False, MOFFS)
for op in range(0xa4, 0xb0):
    ONE[op] = (False, 0)
ONE[0xa8] = (False, 1)
ONE[0xa9] = (False, OPS)
for op in range(0xb0, 0xb8):
    ONE[op] = (False, 1)
for op in range(0xb8, 0xc0):
    ONE[op] = (False, OPS)
ONE[0xc0] = ONE[0xc1] = ONE[0xc6] = (True, 1)
ONE[0xc2] = ONE[0xca] = (False, 2)
ONE[0xc4] = ONE[0xc5] = (True, 0)
ONE[0xc7] = (True, OPS)
ONE[0xc8] = (False, ENTER)
for op in (0xc3, 0xc9, 0xcb, 0xcc, 0xce, 0xcf, 0xd6, 0xd7):
    ONE[op] = (False, 0)
ONE[0xcd] = ONE[0xd4] = ONE[0xd5] = (False, 1)
for op in range(0xd0, 0xd4):
    ONE[op] = (True, 0)
for op in range(0xd8, 0xe0):
    ONE[op] = (True, 0)
for op in range(0xe0, 0xe8):
    ONE[op] = (False, 1)
ONE[0xe8] = ONE[0xe9] = (False, OPS)
ONE[0xea] = (False, PTR)
ONE[0xeb] = (False, 1)
for op in (0xec, 0xed, 0xee, 0xef, 0xf1, 0xf4, 0xf5, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd):
    ONE[op] = (False, 0)
# test, alone of its group, has an immediate (/0 and /1)
ONE[0xf6] = (True, 1)
ONE[0xf7] = (True, OPS)
ONE[0xfe] = ONE[0xff] = (True, 0)
TWO = {}
for op in (0x00, 0x01, 0x02, 0x03, 0x20, 0x21, 0x22, 0x23, 0x24, 0x26, 0xa3, 0xa5, 0xab,
        0xad, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xbb, 0xbc, 0xbd, 0xbe,
        0xbf, 0xc0, 0xc1, 0xc7):
    TWO[op] = (True, 0)
for op in list(range(0x40, 0x50)) + list(range(0x90, 0xa0)):
    TWO[op] = (True, 0)
for op in (0xa4, 0xac, 0xba):
    TWO[op] = (True, 1)
for op in (0x06, 0x08, 0x09, 0x0b, 0x30, 0x31, 0x32, 0x33, 0xa0, 0xa1, 0xa2, 0xa8, 0xa9, 0xaa):
    TWO[op] = (False, 0)
for op in range(0xc8, 0xd0):
    TWO[op] = (False, 0)
for op in range(0x80, 0x90):
    TWO[op] = (False, OPS)


def insn(b, i):
    """The length of the instruction at b[i] in 32-bit code, which may
    run past b's end, and whether it is a call or jump with a 32-bit
    displacement, its last 4 bytes (E8, E9, 0F 80 to 0F 8F). The length
    is 0 for an opcode outside the tables, and where b ends before the
    opcode, ModRM or SIB byte."""
    n = len(b)
    p = i
    osize = asize = 4
    while p < n and b[p] in PREFIXES:
        if b[p] == 0x66:
            osize = 2
        elif b[p] == 0x67:
            asize = 2
        p += 1
    if p >= n:
        return 0, False
    op = b[p]
    p += 1
    if op == 0x0f:
        if p >= n:
            return 0, False
        spec = TWO.get(b[p])
        rel = 0x80 <= b[p] <= 0x8f
        p += 1
    else:
        spec = ONE.get(op)
        rel = op in (0xe8, 0xe9)
    if spec is None:
        return 0, False
    modrm, imm = spec
    if modrm:
        if p >= n:
            return 0, False
        mod, reg, rm = b[p] >> 6, (b[p] >> 3) & 7, b[p] & 7
        p += 1
        if mod != 3 and asize == 4:
            if rm == 4:
                if p >= n:
                    return 0, False
                if mod == 0 and b[p] & 7 == 5:
                    p += 4
                p += 1
            elif mod == 0 and rm == 5:
                p += 4
            p += (0, 1, 4)[mod]
        elif mod != 3:
            if mod == 0 and rm == 6:
                p += 2
            p += (0, 1, 2)[mod]
        if op in (0xf6, 0xf7) and reg > 1:
            imm = 0
    if imm == OPS:
        imm = osize
    elif imm == PTR:
        imm = osize + 2
    elif imm == MOFFS:
        imm = asize
    elif imm == ENTER:
        imm = 3
    return p + imm - i, rel and osize == 4


def calls(ia, va, b):
    """The calls and jumps with a 32-bit displacement of the function at
    va whose bytes are b, as a map from each displacement's offset to its
    instruction's, found by decoding its instructions from its start: a
    fixup site where an instruction would start is data (a jump table),
    and a byte outside the tables is passed over. A displacement may run
    past b's end."""
    key = (va, len(b))
    if key in ia.decoded:
        return ia.decoded[key]
    found = {}
    i = 0
    while i < len(b):
        k = ia.fixat.get(va + i)
        if k:
            i += k
            continue
        size, rel = insn(b, i)
        if not size:
            i += 1
            continue
        if rel:
            found[i + size - 4] = i
        i += size
    ia.decoded[key] = found
    return found


def relmask(ia, va, a, b):
    """Offsets of the displacements of the calls and jumps in a, the bytes
    of the function at va in ia, whose opcodes b has at the same place."""
    m = set()
    for d, s in calls(ia, va, a).items():
        if d + 4 <= len(a) and a[s:d] == b[s:d]:
            m.update(range(d, d+4))
    return m


# Pairs of opcodes that encode the same register-to-register operation
# with the operands swapped (the direction bit): add, or, adc, sbb, and,
# sub, xor, cmp and mov, byte and word forms.
SWAPPED = {}
for op in (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x88):
    for w in (0, 1):
        SWAPPED[op + w] = op + 2 + w
        SWAPPED[op + 2 + w] = op + w


def nop(b, i):
    """Length of the do-nothing instruction assemblers pad with at b[i]:
    nop (1 byte), mov r,r (2), lea r,[r+0] or cs: mov r,r (3), lea
    esp,[esp+0] (4), lea r,[r+0] with a 32-bit displacement (6, or 7
    for esp), else 0."""
    n = len(b) - i
    if n >= 1 and b[i] == 0x90:
        return 1
    if n >= 2 and b[i] in (0x89, 0x8b) and b[i+1] >> 6 == 3 and (b[i+1] >> 3) & 7 == b[i+1] & 7:
        return 2
    if n >= 3 and b[i] == 0x2e and nop(b, i+1) == 2:
        return 3
    if n >= 2 and b[i] == 0x8d:
        mod, reg, rm = b[i+1] >> 6, (b[i+1] >> 3) & 7, b[i+1] & 7
        if mod == 1 and reg == rm and rm != 4 and n >= 3 and b[i+2] == 0:
            return 3
        if mod == 1 and reg == rm == 4 and n >= 4 and b[i+2] == 0x24 and b[i+3] == 0:
            return 4
        if mod == 2 and reg == rm and rm != 4 and n >= 6 and b[i+2:i+6] == b"\0\0\0\0":
            return 6
        if mod == 2 and reg == rm == 4 and n >= 7 and b[i+2] == 0x24 and b[i+3:i+7] == b"\0\0\0\0":
            return 7
    return 0


def nops(b, i, end):
    """Length of the run of no-ops at b[i], ending by end."""
    k = i
    while k < end:
        n = nop(b, k)
        if not n or k + n > end:
            break
        k += n
    return k - i


def equivalent(a, b, i, end):
    """Length of an equivalent encoding at i in both, or 0. MASM, TASM and
    JWasm pad aligned code with different no-ops and may choose either
    direction for an operation between two registers."""
    n = nops(a, i, end)
    if n and n == nops(b, i, end):
        return n
    if i + 2 <= min(len(a), len(b)) and SWAPPED.get(a[i]) == b[i] and a[i+1] >> 6 == 3 and b[i+1] >> 6 == 3:
        ra, ma = (a[i+1] >> 3) & 7, a[i+1] & 7
        rb, mb = (b[i+1] >> 3) & 7, b[i+1] & 7
        if ra == mb and ma == rb:
            return 2
    return 0


def loose(ia, va, ib, vb, a, b, i, n):
    """At a 32-bit fixup site of one image where the other's fixups are
    not known, 4 if the other holds an address within its own image."""
    for x, vx, y, vy, bx, by in ((ia, va, ib, vb, a, b), (ib, vb, ia, va, b, a)):
        if x.fixat.get(vx + i) == 4 and not y.hasfix and not y.fix.get(vy + i) and i + 4 <= n:
            v, = struct.unpack_from("<I", by, i)
            if y.inside(v):
                return 4
    return 0


def compare(ia, va, ib, vb, n, pairs=None, limit=None):
    """Count the bytes that differ, masking fixups and relative calls.
    Returns the count, the first differing offset and the number of bytes
    accepted as equivalent encodings. Each fixup site matched in an image
    whose fixups are not known adds (a's target, b's value, a's site, b's
    site) to pairs. With a limit, the count stops once past it."""
    a = ia.read(va, n)
    b = ib.read(vb, n)
    if a is None or b is None or len(a) != n or len(b) != n:
        return n, 0, 0
    rel = relmask(ia, va, a, b)
    ndiff = 0
    nequiv = 0
    first = None
    i = 0
    while i < n:
        fa = ia.fix.get(va + i)
        fb = ib.fix.get(vb + i)
        if fa or fb:
            if fa == fb:
                i += 1
                continue
            k = loose(ia, va, ib, vb, a, b, i, n)
            if k:
                if pairs is not None:
                    pairs.append((struct.unpack_from("<I", a, i)[0], struct.unpack_from("<I", b, i)[0],
                        va + i, vb + i))
                i += k
                continue
        elif i in rel or a[i] == b[i]:
            i += 1
            continue
        else:
            # the encoding may start a byte or two earlier (a prefix,
            # the opcode of a swapped pair that happens to match)
            k = 0
            for back in (0, 1, 2):
                if i - back >= 0:
                    k = equivalent(a, b, i - back, n)
                    if k and i - back + k > i:
                        k = i - back + k - i
                        break
                    k = 0
            if k:
                nequiv += k
                i += k
                continue
        ndiff += 1
        if first is None:
            first = i
        if limit is not None and ndiff > limit:
            break
        i += 1
    return ndiff, first, nequiv


def padded(b, i, n):
    """Whether b[i:n] is fill and no-ops only."""
    while i < n:
        if b[i] in (0x00, 0x90, 0xcc):
            i += 1
            continue
        k = nop(b, i)
        if not k or i + k > n:
            return False
        i += k
    return True


def trim(ia, va, n):
    """n less the padding at the end: up to 15 trailing fill bytes, then
    the no-ops after the last return (C3, or C2 and its count)."""
    pat = ia.read(va, n)
    if pat is None or len(pat) < n:
        return n
    while n > 1 and len(pat) - n < 15 and pat[n-1] in (0x00, 0x90, 0xcc) and va + n - 1 not in ia.fix:
        n -= 1
    # a call or jump at the end whose displacement ends in zeros
    for d in calls(ia, va, pat):
        if d <= n < d + 4 <= len(pat):
            n = d + 4
            break
    for r in range(n - 1, max(0, n - 17), -1):
        end = 0
        if pat[r] == 0xc3:
            end = r + 1
        elif pat[r] == 0xc2 and r + 3 <= n:
            end = r + 3
        if end and end < n and not any(va + k in ia.fix for k in range(end, n)) and padded(pat, end, n):
            return end
    return n


def search(ia, va, n, ib):
    """Every address in b's code where the n bytes at va in a match, and
    the number of bytes compared."""
    hits = []
    n = trim(ia, va, n)
    pat = ia.read(va, n)
    if pat is None:
        return hits, n
    disp = set()
    for d in calls(ia, va, pat):
        disp.update(range(d, d+4))
    anchor = None
    for i in range(n):
        if va + i not in ia.fix and i not in disp:
            anchor = i
            break
    if anchor is None:
        return hits, n
    for base, data in ib.code():
        start = 0
        while True:
            p = data.find(pat[anchor:anchor+1], start)
            if p < 0:
                break
            start = p + 1
            vb = base + p - anchor
            if vb < base or vb + n > base + len(data):
                continue
            if compare(ia, va, ib, vb, n, limit=0)[0] == 0:
                hits.append(vb)
    return hits, n


def nearest(ia, va, n, ib):
    """The place in b's code where the n bytes at va in a come nearest,
    and how many bytes differ there. None if no run of 8 bytes is shared."""
    pat = ia.read(va, n)
    if pat is None:
        return None
    grams = ib.index()
    votes = collections.Counter()
    for i in range(n - 7):
        if any(va + i + k in ia.fix for k in range(8)):
            continue
        for p in grams.get(pat[i:i+8], ()):
            votes[p - i] += 1
    best = None
    for vb, _ in votes.most_common(8):
        ndiff = compare(ia, va, ib, vb, n)[0]
        if best is None or ndiff < best[1]:
            best = (vb, ndiff)
    return best


def sums(ia, syms):
    """The lines of -s: name, size and CRC-32 of each symbol."""
    starts = {}
    for va, typ, name, size in syms:
        starts.setdefault(va, name)
    funcs = {va for va, typ, name, size in syms if typ.upper() == "T"}
    ranges = sorted((va, name, size) for va, typ, name, size in syms if typ.upper() == "T")
    rangestarts = [va for va, name, size in ranges]
    lines = []
    for va, typ, name, size in syms:
        code = typ.upper() == "T"
        n = trim(ia, va, size) if code else size
        b = ia.read(va, n)
        if b is None or len(b) != n:
            fail("%s: %#x+%d is outside the image" % (name, va, n))
        raw = bytearray(b)
        refs = []
        at = {s: d for d, s in calls(ia, va, b).items()} if code else {}
        i = 0
        while i < n:
            site = va + i
            k = ia.fixat.get(site)
            if k:
                kind = ia.fixkind.get(site, "off32")
                tgt = struct.unpack_from("<I", b, i)[0] if k == 4 and i + 4 <= n else None
                if tgt in starts:
                    ref = starts[tgt]
                elif code and tgt is not None and va <= tgt < va + size:
                    ref = "+%#x" % (tgt - va)
                else:
                    ref = kind
                refs.append("%d %s" % (i, ref))
                raw[i:i+k] = bytes(min(k, n - i))
                i += k
                continue
            if site in ia.fix:
                raw[i] = 0
                i += 1
                continue
            d = at.get(i)
            if d is not None and d + 4 > n:
                # a displacement cut by the next symbol, which starts in it
                refs.append("%d cut" % d)
                raw[d:n] = bytes(n - d)
                break
            if d is not None and not any(va + d + j in ia.fix for j in range(4)):
                tgt = va + d + 4 + struct.unpack_from("<i", b, d)[0]
                ref = None
                if tgt in funcs and not va <= tgt < va + size:
                    ref = starts[tgt]
                elif not va <= tgt < va + size:
                    j = bisect.bisect_right(rangestarts, tgt) - 1
                    if j >= 0 and tgt < ranges[j][0] + ranges[j][2]:
                        ref = "%s+%#x" % (ranges[j][1], tgt - ranges[j][0])
                if ref is not None:
                    refs.append("%d %s" % (d, ref))
                    raw[d:d+4] = bytes(4)
                    i = d + 4
                    continue
            i += 1
        crc = zlib.crc32(bytes(raw) + b"\0" + "\n".join(refs).encode("latin-1"))
        lines.append((name, va, "%s\t%d\t%08x" % (name, n, crc)))
    return [line for name, va, line in sorted(lines)]


USAGE = "usage: lecmp.py [-a [-m n] [-d] [-l log]] a a.sym b [b.sym] | -s a a.sym"


def main(args):
    anchor = False
    near = False
    log = None
    minlen = 16
    sumsonly = False
    while args and args[0].startswith("-"):
        a = args.pop(0)
        if a == "-a":
            anchor = True
        elif a == "-s":
            sumsonly = True
        elif a == "-d":
            near = True
        elif a == "-m" and args:
            minlen = int(args.pop(0))
        elif a == "-l" and args:
            log = args.pop(0)
        else:
            fail(USAGE)
    if sumsonly:
        if anchor or near or log or len(args) != 2:
            fail(USAGE)
        for line in sums(Image(args[0]), readsyms(args[1])):
            print(line)
        return
    if anchor and len(args) == 3:
        ia = Image(args[0])
        ib = Image(args[2])
        out = open(log, "w") if log else None
        for va, typ, name, size in readsyms(args[1]):
            if typ.upper() != "T" or size < minlen:
                continue
            hits, n = search(ia, va, size, ib)
            line = "%s\t%#x\t%s\t%d" % (name, va, ",".join("%#x" % h for h in hits) or "-", size)
            if near and not hits:
                best = nearest(ia, va, n, ib)
                line += "\t" + ("%#x:%d" % best if best else "-")
            print(line)
            if out and len(hits) == 1:
                pairs = []
                compare(ia, va, ib, hits[0], n, pairs)
                for p in pairs:
                    out.write("%#x\t%#x\t%#x\t%#x\t%s\n" % (p + (name,)))
        if out:
            out.close()
        return
    if anchor or near or log or len(args) != 4:
        fail(USAGE)
    ia = Image(args[0])
    ib = Image(args[2])
    bsyms = {name: (va, size) for va, typ, name, size in readsyms(args[3])}
    for va, typ, name, size in readsyms(args[1]):
        if typ.upper() != "T" or name not in bsyms:
            continue
        vb, sb = bsyms[name]
        n = min(size, sb) if sb else size
        ndiff, first, nequiv = compare(ia, va, ib, vb, n)
        verdict = "diff"
        if ndiff == 0 and (size == sb or not sb):
            verdict = "equiv" if nequiv else "ident"
        elif sb:
            ta = trim(ia, va, size)
            if ta == trim(ib, vb, sb):
                tdiff, tfirst, _ = compare(ia, va, ib, vb, ta)
                if tdiff == 0:
                    verdict, ndiff, first = "pad", 0, None
        print("%s\t%#x\t%#x\t%d\t%s\t%d\t%s" % (name, va, vb, size, verdict, ndiff,
            "-" if first is None else "%#x" % first))


if __name__ == "__main__":
    main(sys.argv[1:])
