"""lbapatch.py: the fixes of the game's data that the port makes as it
loads it, written into the files, for LBA.EXE, LbaWin, the Classic and
the Enhanced Edition: the definitive data.

    lbapatch.py -l gamedir...
    lbapatch.py [-s set]... [-p psdir] -o outdir gamedir...

The game directories are read in turn, as the port reads -g, the first
that holds a file giving it. -l lists each data file the sets read: its
name, its path, its form, known by its SHA-256, else unknown, and the
sets that change it (- for none, FILE3D.HQR, which the face sets read
alone), or missing where no directory holds it.

The files the sets change are written into outdir, which must exist
and be none of the game directories, whose files it would replace,
under the names the game opens. Each entry a set can touch is written
in one form of its own, stored, and every other entry's bytes as they
were, so that the definitive files are the same bytes from either CD
and from the stores' installs. A file whose form lies outside the
tool's table is refused, and so is an output whose form the table
lacks. The port reads outdir first with -g, and another version takes
the files in place of its own, its originals kept aside. A save of the running scene, the port's with SaveAnywhere, keeps
the CRC-32 of its scene's data, which the scenes set changes: such a
save made before loads as its record.

The sets, without -s scenes, grids and texts:

scenes: SCENE.HQR's values that LoadScene mends: scene 9's hidden mine
and scene 12's door half moved, scene 77's ladder ended, scene 105's
zone renumbered (LbaWin's), scene 20's bonus zone widened (DotEmu's),
three actors put in place and Julia's armour (the PlayStation's).

grids: LBA_GRI.HQR's cells that CopyMapToCube writes, read from
sources/gridfix.c beside the tool, and the layouts that InitGrille names
in their grids' bits: the PlayStation's fixes of scenes 5, 8 and 43, its
parapet on scene 109's ledge and the European CD's cells over the North
American CD's. DotEmu's DOS copy of the North American CD, in GOG's
install (Speedrun\\Windows) and Steam's (DotEmu\\original), keeps the
four armchair caps of scene 5 and the patch of its hall that it took
from the PlayStation.

texts: TEXT.HQR's texts that InitDial replaces, read from
sources/textfix.c: the European CD's over the North American CD's and
over DotEmu's, and the PlayStation's corrections of errors, which -p
takes from the disc instead where the port takes its text whole. The
North American CD's four stray texts of bank 8, which the European CD
drops, go.

face and face-full, outside the defaults, one or the other: Twinsen's
bodies (BODY.HQR's 0 to 8, and 114, half of him) with the PlayStation
version's remodel, read from its disc with -p: face its face alone, the
eyes, nose, brow and chin on the PC's head, the PC's spheres kept for
the bun, the ponytail and the hands, face-full its head, hair and hands
whole, the bun, the ponytail and the hands made shapes, as its renderer
draws its spheres. A normal taken from the PlayStation is turned from
its 64 units to the PC's 63 and divided by the low 10 bits of its
divisor, as the PlayStation lights it. Both take the PC's BODY.HQR
alone, and each body is read back and checked against the renderer's
room.

-p names a directory holding the PlayStation disc's files, as
tools/isocat.py copies them out: TEXT.HQR, GAME.HQR and SCENES.BLK.
"""
import collections
import hashlib
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCES = os.path.join(HERE, "..", "sources")
SIDE = 64
HIGH = 25
LANGS = ("EN", "FR", "DE", "SP", "IT")
BANKS = 14

# Each file's known forms: its SHA-256, the name of the form.
FORMS = {
    "SCENE.HQR": {
        "a320c359fd41ef2b1be93f885a840615c316c83be50e5e81e90f93dbb647c859": "every PC version",
        "edc29fc3b1483830fd2774f8019d50b4813698eb7867876ee3b94340a2cf32ab": "the definitive",
    },
    "LBA_GRI.HQR": {
        "c1880cfc14f6a136fc43836f2d48b8db5a9b6238dc9e40bf2677e6c6f05e6e1f": "the European CD",
        "d76cd521e08a3eb28ec43926d0db81e3abee89355ae9a071bdb9a7b1684a2319": "the North American CD",
        "6e9a83405393d45858372e0aadca675735d885135ce69092257a040a95f5506f": "DotEmu's DOS copy",
        "ace80c099f51e6effc007a303702cc837881535dcb449c5467bc83fcebc920ce": "the definitive",
        "e2d7804b786112d8390b9b124fa72bd8ea35c0cd03cdee8a16738951d903fb93": "DotEmu's DOS copy fixed",
    },
    "BODY.HQR": {
        "3100636a705aadf5d2650759111504fb0d923b3855ccccf027b94d211336bd49": "every PC version",
        "e31370b842e6ff28a85e74bca41ed1540779406bc8623fe23f965725db0e10ad": "the PlayStation's face",
        "71a00fdd94b0987e1a211e394c2d6aa62f8d154ce61be7364a861067883f762d": "the PlayStation's head",
    },
    "FILE3D.HQR": {
        "74abc1a90f1543dc0c2d8141ba7b0c6f773c29b71ab8e782910bd31103617d2d": "every PC version",
    },
    "TEXT.HQR": {
        "06688f7d9e7424fd486d21dd5c6234f0ebbd18f0eeddbc81089e364dad69d656": "the European CD",
        "ec60aa7323a2e8daa270bda71db9db1dff4062b2048c37dc7ff9f6c641667cd2": "the North American CD",
        "79c2a12b41a4b0656e3ccfc277d88b671e5909f275ea665574696ef41bd1547f": "DotEmu",
        "56bb7a5a7b57dda2c59bb147aa055961e1778743b3915b23a84991e936fe0240": "the definitive",
    },
}

# The PlayStation disc's files the sets read, SLES_006.98's.
PSFORMS = {
    "TEXT.HQR": "cc66d1dad46ed6870d670c546b48b876948e144b5f9df70a52eb2a28bf31864d",
    "GAME.HQR": "dbcb210c702c3d3697e5da48195baa90d8cade92616359393c4f3eab15c716b9",
    "SCENES.BLK": "4b2692ca7a98d539a33bb1ffa325351141e7bd979ed8862847a71189688ff9af",
}

# The PlayStation's font's codes for the characters it lacks, and their
# CP437 bytes.
PSFONT = {0x17: 0x8D, 0x18: 0xA1, 0x19: 0xF8, 0x1A: 0xE1}

# The values LoadScene writes over a scene's: the scene, an actor's or a
# zone's number, the field and its value. An actor's fields lie at
# these offsets of its 35 bytes, a zone's at these of its 24.
ACTOR = {"X": 8, "Y": 10, "Z": 12, "Armure": 33}
ZONE = {"X1": 6, "Z1": 10, "Num": 14}
SCENES = (
    (9, "actor", 21, "X", 6912), (9, "actor", 21, "Z", 768),
    (9, "actor", 11, "Y", 1024),
    (12, "actor", 29, "Z", 1795), (12, "actor", 18, "Y", 2048),
    (20, "zone", 4, "X1", 24305),
    (42, "actor", 22, "Y", 3584), (42, "actor", 22, "Z", 3508),
    (58, "actor", 1, "Armure", 51),
    (77, "zone", 6, "Z1", 3616),
    (105, "zone", 11, "Num", 50),
)

# The North American CD's texts of bank 8 in Spanish and in Italian that
# the European CD drops, which no other language holds and no script
# asks for: each a language, a bank and a number.
STRAYS = {(3, 8, 18), (3, 8, 26), (4, 8, 18), (4, 8, 26)}

SETS = ("scenes", "grids", "texts", "face", "face-full")
DEFAULT = ("scenes", "grids", "texts")


class Fail(Exception):
    pass


def u16(b, p):
    return struct.unpack_from("<H", b, p)[0]


def u32(b, p):
    return struct.unpack_from("<I", b, p)[0]


def i16(b, p):
    return struct.unpack_from("<h", b, p)[0]


def expand(src, size, add):
    """LZS's bytes expanded to size, each copy add bytes longer than its
    count: 2 the PC's, 3 the PlayStation's."""
    out = bytearray()
    s = 0
    while len(out) < size:
        flags = src[s]
        s += 1
        for _ in range(8):
            if len(out) >= size:
                break
            if flags & 1:
                out.append(src[s])
                s += 1
            else:
                w = src[s] | src[s + 1] << 8
                s += 2
                start = len(out) - (w >> 4) - 1
                for k in range((w & 0xF) + add):
                    out.append(out[start + k])
            flags >>= 1
    return bytes(out[:size])


class Hqr:
    """An HQR file's entries: each its bytes as the file holds them, the
    header and the packed data, and its data expanded, None for an empty
    entry. The last offset of the table is the file's size."""

    def __init__(self, data):
        self.raw = []
        self.data = []
        n = u32(data, 0) // 4
        for k in range(n):
            off = u32(data, 4 * k)
            if off == 0 or off + 10 > len(data):
                self.raw.append(None)
                self.data.append(None)
                continue
            size, packed, method = struct.unpack_from("<IIH", data, off)
            body = data[off + 10:off + 10 + packed]
            self.raw.append(data[off:off + 10 + packed])
            if method == 0:
                self.data.append(body[:size])
            elif method in (1, 2):
                self.data.append(expand(body, size, method + 1))
            else:
                raise Fail("entry %d: method %d" % (k, method))

    def write(self, changed):
        """The file with the entries of changed (an entry's number to its
        data) stored, the others as they were."""
        n = len(self.raw)
        head = bytearray(4 * n)
        body = bytearray()
        for k in range(n):
            if k in changed:
                e = struct.pack("<IIH", len(changed[k]), len(changed[k]), 0) + changed[k]
            else:
                e = self.raw[k]
            if e is None:
                continue
            struct.pack_into("<I", head, 4 * k, 4 * n + len(body))
            body += e
        if n and self.raw[-1] is None:
            struct.pack_into("<I", head, 4 * (n - 1), 4 * n + len(body))
        return bytes(head + body)


def cnumbers(text, name):
    """The numbers of the C array name in text, its comments left out."""
    body = text[text.index(name + "[] = {"):]
    body = body[body.index("{") + 1:body.index("};")]
    while "/*" in body:
        body = body[:body.index("/*")] + body[body.index("*/") + 2:]
    return [int(t) for t in body.replace(",", " ").split()]


def cstrings(text, name):
    """The strings of the C array name in text, each its literals joined
    and its escapes read as CP437 bytes, and whether its comment marks it
    the PlayStation's."""
    body = text[text.index(name + "[] = {"):]
    body = body[body.index("{") + 1:]
    out = []
    mark = False
    cur = None
    i = 0
    while body[i] != "}":
        c = body[i]
        if body.startswith("/*", i):
            j = body.index("*/", i)
            mark = "PlayStation" in body[i:j]
            i = j + 2
        elif c == '"':
            if cur is None:
                cur = bytearray()
            i += 1
            while body[i] != '"':
                if body[i] == "\\":
                    if body[i + 1] in "01234567":
                        cur.append(int(body[i + 1:i + 4], 8))
                        i += 4
                    else:
                        cur.append(ord(body[i + 1]))
                        i += 2
                else:
                    cur.append(ord(body[i]))
                    i += 1
            i += 1
        elif c == ",":
            out.append((bytes(cur) + b"\0", mark))
            cur = None
            mark = False
            i += 1
        else:
            i += 1
    return out


def source(name):
    with open(os.path.join(SOURCES, name), encoding="latin-1") as f:
        return f.read()


# The grids

def column(g, x, z):
    """Column x, z of a grid, its 25 cells, each a layout and a brick, as
    DecompColonne unpacks them."""
    s = u16(g, 2 * (x + SIDE * z))
    runs = g[s] or 256
    s += 1
    cells = []
    for _ in range(runs):
        op = g[s]
        s += 1
        n = (op & 0x3F) + 1
        if op & 0xC0 == 0:
            cells += [(0, 0)] * n
        elif op & 0x40:
            cells += [(g[s + 2 * i], g[s + 2 * i + 1]) for i in range(n)]
            s += 2 * n
        else:
            cells += [(g[s], g[s + 1])] * n
            s += 2
    return (cells + [(0, 0)] * HIGH)[:HIGH]


def packcolumn(cells):
    """A column's cells packed as DecompColonne reads them: empty cells
    in runs of 00, a cell repeated in runs of 10, the rest in runs of 01,
    each run as long as it goes."""
    runs = []
    i = 0
    while i < HIGH:
        j = i
        while j < HIGH and cells[j] == cells[i]:
            j += 1
        if cells[i] == (0, 0):
            runs.append(bytes([j - i - 1]))
        elif j - i >= 2:
            runs.append(bytes([0x80 | j - i - 1]) + bytes(cells[i]))
        else:
            j = i + 1
            while j < HIGH and cells[j] != (0, 0) and (j + 1 >= HIGH or cells[j + 1] != cells[j]):
                j += 1
            runs.append(bytes([0x40 | j - i - 1]) + b"".join(bytes(c) for c in cells[i:j]))
        i = j
    return bytes([len(runs)]) + b"".join(runs)


def packgrid(cols, bits):
    """A grid of 4096 columns, x + 64 z, and its 32 bytes of layouts: a
    word a column, its offset, the columns in that order, a column the
    same as one before sharing its bytes, then the bytes of layouts."""
    offs = []
    body = bytearray()
    seen = {}
    for c in cols:
        p = packcolumn(c)
        if p not in seen:
            seen[p] = 2 * SIDE * SIDE + len(body)
            body += p
        offs.append(seen[p])
    if 2 * SIDE * SIDE + len(body) > 0xFFFF:
        raise Fail("a grid past 64 KB")
    return struct.pack("<%dH" % (SIDE * SIDE), *offs) + bytes(body) + bytes(bits)


def gridrows():
    """sources/gridfix.c's rows: each grid, x, z, y, the cells the grid
    holds and those the fix writes. And its layouts: each grid and
    layout the cells bring."""
    text = source("gridfix.c")
    v = cnumbers(text, "gridfixes")
    rows = []
    i = 0
    while v[i] != 255:
        g, x, z, y, n = v[i:i + 5]
        a = v[i + 5:i + 5 + 2 * n]
        b = v[i + 5 + 2 * n:i + 5 + 4 * n]
        rows.append((g, x, z, y, list(zip(a[::2], a[1::2])), list(zip(b[::2], b[1::2]))))
        i += 5 + 4 * n
    w = cnumbers(text, "gridlayouts")
    lay = [(w[j], w[j + 1]) for j in range(0, len(w) - 1, 2)]
    return rows, lay


def writecells(hqr, rows, lay):
    """The grids rows and lay touch, each with the rows' cells written
    where its column holds the cells they replace, and the layouts lay
    gives it named in its bits."""
    changed = {}
    for g in sorted({r[0] for r in rows} | {l[0] for l in lay}):
        data = hqr.data[g]
        cols = [column(data, x, z) for z in range(SIDE) for x in range(SIDE)]
        for rg, x, z, y, a, b in rows:
            c = cols[x + SIDE * z]
            if rg == g and c[y:y + len(a)] == a:
                cols[x + SIDE * z] = c[:y] + b + c[y + len(b):]
        bits = bytearray(data[-32:])
        for lg, l in lay:
            if lg == g:
                bits[l >> 3] |= 0x80 >> (l & 7)
        changed[g] = packgrid(cols, bits)
    return changed


def fixgrids(hqr, opt):
    rows, lay = gridrows()
    return writecells(hqr, rows, lay)


# The scenes

def places(s):
    """Each actor's offset in a scene of SCENE.HQR, and its zones'."""
    p = 2 + 2 * 18 + 1 + 6
    for _ in range(2):
        p += 2 + u16(s, p)
    actors = [None]
    nobj = u16(s, p)
    p += 2
    for _ in range(1, nobj):
        actors.append(p)
        p += 35
        for _ in range(2):
            p += 2 + u16(s, p)
    zones = p + 2
    return actors, zones


def fixscenes(hqr, opt):
    changed = {}
    for sc in sorted({v[0] for v in SCENES}):
        s = bytearray(hqr.data[sc])
        actors, zones = places(s)
        for vsc, kind, n, field, value in SCENES:
            if vsc != sc:
                continue
            if kind == "actor":
                at = actors[n] + ACTOR[field]
            else:
                at = zones + 24 * n + ZONE[field]
            if field == "Armure":
                s[at] = value
            else:
                struct.pack_into("<h", s, at, value)
        changed[sc] = bytes(s)
    return changed


# The texts

def textrows():
    """sources/textfix.c's rows: each language, bank and number, the
    CRC-32s of the forms the row replaces, its text, and whether the text
    is the PlayStation's."""
    text = source("textfix.c")
    v = cnumbers(text, "textfixes")
    ss = cstrings(text, "fixedtexts")
    rows = []
    i = 0
    while v[i] != 255:
        n = v[i + 4]
        forms = {v[i + 5 + 4 * k] | v[i + 6 + 4 * k] << 8 | v[i + 7 + 4 * k] << 16 | v[i + 8 + 4 * k] << 24
                 for k in range(n)}
        t, mark = ss[len(rows)]
        rows.append(((v[i], v[i + 1], v[i + 2] | v[i + 3] << 8), forms, t, mark))
        i += 5 + 4 * n
    if len(rows) != len(ss):
        raise Fail("sources/textfix.c: %d rows, %d texts" % (len(rows), len(ss)))
    return rows


def psfile(psdir, name):
    """The PlayStation disc's file name, its form checked."""
    path = os.path.join(psdir, name)
    with open(path, "rb") as f:
        data = f.read()
    if hashlib.sha256(data).hexdigest() != PSFORMS[name]:
        raise Fail("%s: a form outside the table" % path)
    return data


def pstexts(psdir):
    """The PlayStation disc's texts, its font's codes made CP437 again,
    each by its language, bank and number."""
    out = {}
    h = Hqr(psfile(psdir, "TEXT.HQR"))
    for lang in range(len(LANGS)):
        for bank in range(BANKS):
            k = 2 * (lang * BANKS + bank)
            for num, t in banktexts(h.data[k], h.data[k + 1]):
                out[(lang, bank, num)] = bytes(PSFONT.get(c, c) for c in t)
    return out


def banktexts(order, text):
    """A bank's texts, each its number and its bytes with their NUL."""
    nums = [u16(order, 2 * i) for i in range(len(order) // 2)]
    offs = [u16(text, 2 * i) for i in range(len(nums) + 1)]
    return [(n, text[offs[i]:offs[i + 1]]) for i, n in enumerate(nums)]


def fixtexts(hqr, opt):
    """The banks the rows touch, each with a text replaced where its
    CRC-32 is one of its row's and the strays left out."""
    rows = textrows()
    if opt["psdir"] is not None:
        ps = pstexts(opt["psdir"])
        rows = [(k, f, ps[k] if mark else t, mark) for k, f, t, mark in rows]
    changed = {}
    for lang, bank in sorted({r[0][:2] for r in rows} | {s[:2] for s in STRAYS}):
        k = 2 * (lang * BANKS + bank)
        nums = []
        texts = []
        for num, t in banktexts(hqr.data[k], hqr.data[k + 1]):
            if (lang, bank, num) in STRAYS:
                continue
            for key, forms, d, _ in rows:
                if key == (lang, bank, num) and zlib.crc32(t) in forms:
                    t = d
                    break
            nums.append(num)
            texts.append(t)
        order = struct.pack("<%dH" % len(nums), *nums)
        offs = []
        at = 2 * (len(texts) + 1)
        for t in texts:
            offs.append(at)
            at += len(t)
        offs.append(at)
        changed[k] = order
        changed[k + 1] = struct.pack("<%dH" % len(offs), *offs) + b"".join(texts)
    return changed


# The face

TWINSEN = (0, 1, 2, 3, 4, 5, 6, 7, 8, 114)
SPRITE_3D = 0x400

# The 1994 renderer's room (p_ob_iso.c): points and normals a body
# (List_Point, List_Normal), bytes of List_Entity (a polygon 4 and 6 a
# point, a line or a sphere 12), entries of List_Tri, and points a
# polygon (TabPoly, s_poly.c).
NPOINT = 500
NENTITY = 2 * 5000
NTRI = 1000
NPOLYPOINT = 33

# The PC's normals are 63 units long, the PlayStation's 64.
PCUNIT = 63
PSUNIT = 64

# The lists of FUN_80021efc after the count of polygons: a name, the
# halfwords of an entry (0 for a flat one: a colour, a normal when its
# bit 0x4000 is set, then its points), the halfwords holding its points,
# and the halfword whose sign marks a texture window (None for the first
# four lists).
LISTS = (
    ("F3", 0, (), None), ("F4", 0, (), None),
    ("G3", 7, (2, 4, 6), None), ("G4", 9, (2, 4, 6, 8), None),
    ("FT3", 6, (0, 2, 4), 0), ("FT4", 8, (0, 2, 4, 6), 0),
    ("FT3l", 7, (1, 3, 5), 0), ("FT4l", 9, (1, 3, 5, 7), 0),
    ("GT3", 9, (1, 4, 7), 0), ("GT4", 12, (1, 4, 7, 10), 0),
    ("x4", 4, (), 0), ("x5", 5, (), 0), ("x5", 5, (), 0), ("x6", 6, (), 0),
    ("GT3e", 10, (3, 6, 9), 0), ("GT4e", 13, (3, 6, 9, 12), 0),
    ("FT3", 6, (0, 2, 4), 0), ("FT4", 8, (0, 2, 4, 6), 0),
    ("FT3l", 7, (1, 3, 5), 0), ("FT4l", 9, (1, 3, 5, 7), 0),
    ("GT3", 9, (1, 4, 7), 0), ("GT4", 12, (1, 4, 7, 10), 0),
    ("x4", 4, (), 2), ("x5", 5, (), 2), ("x5", 5, (), 2), ("x6", 6, (), 2),
    ("GT3e", 10, (3, 6, 9), 2), ("GT4e", 13, (3, 6, 9, 12), 2),
)


def table(b):
    """The items of a part that opens on a table of 32-bit offsets, the
    first giving their count."""
    n = u32(b, 0) // 4
    offs = [u32(b, 4 * k) for k in range(n)] + [len(b)]
    return [b[offs[k]:offs[k + 1]] for k in range(n)]


class Ps:
    """The PlayStation's scenes: GAME.HQR's entry 10 gives a size and a
    stored size for each of the 9 parts of each scene, laid in
    SCENES.BLK one after the other, parts 0 and 3 from a 2048-byte
    sector, a part stored smaller packed."""

    def __init__(self, psdir):
        with open(os.path.join(psdir, "GAME.HQR"), "rb") as f:
            t = Hqr(f.read()).data[10]
        self.parts = []
        at = 0
        for i in range(len(t) // 8):
            size, kept = struct.unpack_from("<II", t, 8 * i)
            if i % 9 in (0, 3):
                at = (at + 0x7ff) & ~0x7ff
            self.parts.append((at, size, kept))
            at += kept
        self.scenes = len(self.parts) // 9
        self.blk = open(os.path.join(psdir, "SCENES.BLK"), "rb")

    def part(self, scene, k):
        at, size, kept = self.parts[9 * scene + k]
        self.blk.seek(at)
        raw = self.blk.read(kept)
        return expand(raw, size, 3) if kept < size else raw

    def close(self):
        self.blk.close()


def file3d(b):
    """A File3D's entries: (type, generic number) to the number of the
    body or animation, the first of each winning."""
    out = {}
    p = 0
    while b[p] != 0xFF:
        out.setdefault((b[p], b[p + 1]), i16(b, p + 3))
        p += 2 + b[p + 2]
    return out


def pcactors(s):
    """The flags and File3D of each actor of a PC scene but the hero."""
    p = 45
    for _ in range(2):
        p += 2 + u16(s, p)
    n = u16(s, p)
    p += 2
    out = []
    for _ in range(1, n):
        out.append(struct.unpack_from("<HH", s, p))
        p += 35
        for _ in range(2):
            p += 2 + u16(s, p)
    return out


def psactors(scx):
    """The flags and File3D of each actor of a PlayStation scene but the
    hero."""
    n = i16(scx, 0x24)
    return [struct.unpack_from("<Hh", scx, 0x2c + 44 * k) for k in range(1, n)]


def psbodies(pcf, pcs, ps):
    """For each PC body, its PlayStation versions: their bytes, the PC's
    File3Ds pcf and scenes pcs pairing them."""
    out = collections.defaultdict(dict)
    for sc in range(ps.scenes):
        fix = table(ps.part(sc, 7))
        items = table(ps.part(sc, 3))
        links = [(k, k) for k in range(5)]
        for (fa, a), (fb, b) in zip(pcactors(pcs[sc]), psactors(ps.part(sc, 8))):
            if not fa & SPRITE_3D:
                links.append((a, b))
        for a, b in links:
            ea, eb = file3d(pcf[a]), file3d(fix[b])
            for key in ea.keys() & eb.keys():
                if key[0] == 1:
                    out[ea[key]].setdefault(items[eb[key]], (sc, eb[key]))
    return out


def pcbody(b):
    """A PC body as p_ob_iso.c reads it, each record's bytes kept: the
    header of 16 bytes (the flags, the box, the size of the block of
    info) and the info, the points (3 words), the groups (38 bytes: the
    first point and the origin as offsets of 6, the parent as one of
    38, the type and three angles, at 18 the count of normals), the
    normals (x, y, z and the divisor of the light), the polygons (the
    material, the count of points, the colour word, then a normal and a
    point a corner from material 9, a normal then the points for 7 and
    8, the points for the others), the lines and spheres (8 bytes: a
    word whose high byte is the colour, a word, two points or the
    radius and the centre), and the bytes after them."""
    r = {"head": b[:16 + u16(b, 14)], "flags": u16(b, 0)}
    p = 16 + u16(b, 14)
    n = u16(b, p)
    p += 2
    r["points"] = [struct.unpack_from("<3h", b, p + 6 * k) for k in range(n)]
    p += 6 * n
    r["groups"] = []
    if r["flags"] & 2:
        n = u16(b, p)
        p += 2
        nf = 0
        for k in range(n):
            raw = b[p + 38 * k:p + 38 * (k + 1)]
            g = struct.unpack_from("<10h", raw)
            r["groups"].append({"first": g[0] // 6, "count": g[1], "origin": g[2] // 6,
                                "parent": g[3] // 38 if g[3] >= 0 else -1,
                                "nfirst": nf, "ncount": g[9], "raw": raw})
            nf += g[9]
        p += 38 * n
    n = u16(b, p)
    p += 2
    r["normals"] = [struct.unpack_from("<4h", b, p + 8 * k) for k in range(n)]
    p += 8 * n
    n = u16(b, p)
    p += 2
    r["polys"] = []
    for _ in range(n):
        mat, cnt, word = b[p], b[p + 1], u16(b, p + 2)
        if mat >= 9:
            nn = [u16(b, p + 4 + 4 * k) for k in range(cnt)]
            pts = [u16(b, p + 6 + 4 * k) // 6 for k in range(cnt)]
            size = 4 + 4 * cnt
            shade = "G"
        elif mat >= 7:
            nn = [u16(b, p + 4)]
            pts = [u16(b, p + 6 + 2 * k) // 6 for k in range(cnt)]
            size = 6 + 2 * cnt
            shade = "L"
        else:
            nn = []
            pts = [u16(b, p + 4 + 2 * k) // 6 for k in range(cnt)]
            size = 4 + 2 * cnt
            shade = "F"
        r["polys"].append({"mat": mat, "word": word, "colour": word & 0xFF, "shade": shade,
                           "normals": nn, "pts": pts})
        p += size
    for part in ("lines", "spheres"):
        n = u16(b, p)
        p += 2
        r[part] = [struct.unpack_from("<4H", b, p + 8 * k) for k in range(n)]
        p += 8 * n
    r["lines"] = [(w0, w1, i // 6, j // 6) for w0, w1, i, j in r["lines"]]
    r["spheres"] = [(w0, w1, rad, c // 6) for w0, w1, rad, c in r["spheres"]]
    r["tail"] = b[p:]
    return r


def psbody(b):
    """A PlayStation body as FUN_80025100 and FUN_80021efc read it:
    "3DP!", a word, the box, the points (a word's count, 4 halfwords
    each), the groups (a word's count, 8 halfwords each: the first
    point, the points, the origin, the parent times 16, the first
    normal, the normals, two of 0), the normals (a word's count, 4
    halfwords each, the last the divisor), the count of polygons, then
    if any the lists, then the lines (a count, 4 halfwords each). The
    groups name points and normals by offsets of 8, the polygons and
    lines points by offsets of 8 and normals by their numbers, and a
    colour is 4 times its index, with 0x4000 for a flat polygon lit by
    a normal."""
    if b[:4] != b'3DP!':
        raise Fail("no 3DP! at its start")
    r = {}
    n = u32(b, 0x14)
    r["points"] = [struct.unpack_from("<3h", b, 0x18 + 8 * k) for k in range(n)]
    p = 0x18 + 8 * n
    n = u32(b, p)
    r["groups"] = []
    for k in range(n):
        g = struct.unpack_from("<8h", b, p + 4 + 16 * k)
        r["groups"].append({"first": g[0] // 8, "count": g[1], "origin": g[2] // 8,
                            "parent": g[3] // 16, "nfirst": g[4] // 8, "ncount": g[5]})
    p += 4 + 16 * n
    n = u32(b, p)
    r["normals"] = [struct.unpack_from("<4h", b, p + 4 + 8 * k) for k in range(n)]
    p += 4 + 8 * n
    count = i16(b, p)
    p += 2
    r["polys"] = []
    if count != 0:
        for name, size, at, mark in LISTS:
            cnt = i16(b, p)
            p += 2
            while cnt > 0:
                if mark is not None and i16(b, p + 2 * mark) < 0:
                    p += 6
                    continue
                c = u16(b, p)
                if size == 0:
                    lit = bool(c & 0x4000)
                    k = 1 + lit
                    np_ = 3 if name == "F3" else 4
                    pts = [u16(b, p + 2 * (k + j)) // 8 for j in range(np_)]
                    r["polys"].append({"list": name, "word": c, "colour": (c & 0x3ff) // 4,
                                       "shade": "L" if lit else "F",
                                       "normals": [u16(b, p + 2)] if lit else [], "pts": pts})
                    p += 2 * (k + np_)
                elif name in ("G3", "G4"):
                    r["polys"].append({"list": name, "word": c, "colour": (c & 0x3ff) // 4, "shade": "G",
                                       "normals": [u16(b, p + 2 * j - 2) for j in at],
                                       "pts": [u16(b, p + 2 * j) // 8 for j in at]})
                    p += 2 * size
                else:
                    raise Fail("a textured polygon in list %s" % name)
                cnt -= 1
    return r


def owners(r):
    """Each point's group."""
    own = [-1] * len(r["points"])
    for k, g in enumerate(r["groups"]):
        for i in range(g["first"], g["first"] + g["count"]):
            own[i] = k
    return own


def normalowners(r):
    """Each normal's group."""
    own = [-1] * len(r["normals"])
    for k, g in enumerate(r["groups"]):
        for i in range(g["nfirst"], g["nfirst"] + g["ncount"]):
            own[i] = k
    return own


def named(r):
    """Each point as its group and coordinates."""
    return [(g,) + tuple(p) for g, p in zip(owners(r), r["points"])]


def world(r):
    """Each point in the rest pose."""
    out = [None] * len(r["points"])
    for k, g in enumerate(r["groups"]):
        o = out[g["origin"]] if k > 0 else (0, 0, 0)
        for i in range(g["first"], g["first"] + g["count"]):
            out[i] = tuple(o[j] + r["points"][i][j] for j in range(3))
    return out


def under(r, top):
    """Group top and the groups under it."""
    keep = {top}
    for k, g in enumerate(r["groups"]):
        if g["parent"] in keep:
            keep.add(k)
    return keep


def turn(pts, k):
    return pts[k:] + pts[:k]


def key(nm, p):
    """A polygon by its named points in turn from its least, its colour
    and its shading."""
    pts = [nm[i] for i in p["pts"]]
    return (min(tuple(turn(pts, k)) for k in range(len(pts))), p["colour"], p["shade"])


class Pair:
    """A PC body and its PlayStation version, and what the PlayStation
    changes: the groups whose points differ or whose polygons do, and
    the part each plays."""

    def __init__(self, n, a, b):
        self.n, self.a, self.b = n, a, b
        self.na, self.nb = named(a), named(b)
        self.oa, self.ob = owners(a), owners(b)
        if len(a["groups"]) != len(b["groups"]):
            raise Fail("body %d: %d groups and %d" % (n, len(a["groups"]), len(b["groups"])))
        self.ka = collections.Counter(key(self.na, p) for p in a["polys"])
        self.kb = collections.Counter(key(self.nb, p) for p in b["polys"])
        self.polyout = self.ka - self.kb
        self.polyin = self.kb - self.ka
        ptout = collections.Counter(self.na) - collections.Counter(self.nb)
        ptin = collections.Counter(self.nb) - collections.Counter(self.na)
        self.ptin = ptin
        change = collections.Counter()
        for p in list(ptout.elements()) + list(ptin.elements()):
            change[p[0]] += 1
        touched = set()
        for (pts, _, _) in list(self.polyout) + list(self.polyin):
            touched |= {p[0] for p in pts}
        self.changed = set(change) | touched
        self.head = max(self.changed, key=lambda k: (change[k], -k))
        hair = under(a, self.head) - {self.head}
        self.part = {}
        for k in sorted(self.changed):
            if k == self.head:
                self.part[k] = "head"
            elif k in hair:
                self.part[k] = "hair"
            elif any(self.oa[c] == k and self.shaped(c, rad) for _, _, rad, c in a["spheres"]):
                self.part[k] = "hand"
            else:
                self.part[k] = "other"

    def shaped(self, c, rad):
        """The PlayStation's new points about PC sphere centre c of radius rad."""
        cen = self.na[c]
        return [p for p in self.ptin.elements() if p[0] == cen[0] and math.dist(p[1:], cen[1:]) <= 2 * rad]


def pairs(bodies, pcf, pcs, psdir):
    """Twinsen's bodies paired."""
    ps = Ps(psdir)
    try:
        found = psbodies(pcf, pcs, ps)
    finally:
        ps.close()
    out = []
    for n in TWINSEN:
        if len(found[n]) != 1:
            raise Fail("body %d: %d PlayStation versions" % (n, len(found[n])))
        out.append(Pair(n, pcbody(bodies[n]), psbody(next(iter(found[n])))))
    return out


def psnormal(v):
    """A normal of the PlayStation as the PC lights it: 63 units long,
    its divisor the low 10 bits the PlayStation divides by, none of
    them a light of 0."""
    x, y, z, d = v
    d &= 0x3FF
    if d == 0:
        return (0, 0, 0, 1)
    return tuple(int(round(c * PCUNIT / PSUNIT)) for c in (x, y, z)) + (d,)


def build(pr, face):
    """PC body pr.a with the PlayStation's head, hair and hands (face:
    the head alone, the bun's shape left out for the PC's sphere): its
    parts, to be packed."""
    a, b, na, nb, oa, ob = pr.a, pr.b, pr.na, pr.nb, pr.oa, pr.ob
    take = {pr.head} if face else {k for k, part in pr.part.items() if part in ("head", "hair", "hand")}
    drop = set()
    dropsph = set()
    for s, (w0, w1, rad, c) in enumerate(a["spheres"]):
        if oa[c] not in take:
            continue
        if face:
            shape = set(pr.shaped(c, rad))
            drop |= {j for j in range(len(b["points"])) if nb[j] in shape}
        else:
            dropsph.add(s)

    # the points, group by group, each new one by its side and number
    where = collections.defaultdict(list)
    for j, p in enumerate(nb):
        where[p].append(j)
    pcwhere = collections.defaultdict(list)
    for i, p in enumerate(na):
        pcwhere[p].append(i)
    newpt = {}
    points = []
    span = []
    for k in range(len(a["groups"])):
        if k in take:
            g = b["groups"][k]
            src = [("b", j) for j in range(g["first"], g["first"] + g["count"]) if j not in drop]
        else:
            g = a["groups"][k]
            src = [("a", i) for i in range(g["first"], g["first"] + g["count"])]
        span.append((len(points), len(src)))
        for s in src:
            newpt[s] = len(points)
            points.append((a if s[0] == "a" else b)["points"][s[1]])

    def pcpoint(i):
        if oa[i] not in take:
            return newpt["a", i]
        for j in where[na[i]]:
            if ("b", j) in newpt:
                return newpt["b", j]
        raise Fail("body %d: PC point %d %s has no twin taken" % (pr.n, i, na[i]))

    def pspoint(j):
        if ob[j] in take:
            return newpt["b", j]
        for i in pcwhere[nb[j]]:
            return newpt["a", i]
        raise Fail("body %d: PlayStation point %d %s has no twin on the PC" % (pr.n, j, nb[j]))

    # the normals, group by group
    noa, nob = normalowners(a), normalowners(b)
    newn = {}
    normals = []
    ncount = []
    for k in range(len(a["groups"])):
        if k in take:
            g = b["groups"][k]
            src = [("b", j) for j in range(g["nfirst"], g["nfirst"] + g["ncount"])]
        else:
            g = a["groups"][k]
            src = [("a", i) for i in range(g["nfirst"], g["nfirst"] + g["ncount"])]
        ncount.append(len(src))
        for s in src:
            newn[s] = len(normals)
            normals.append(a["normals"][s[1]] if s[0] == "a" else psnormal(b["normals"][s[1]]))

    # each normal of the PlayStation's in the groups kept, as the PC's
    # at the same corner of the same polygon
    pcpoly = {}
    for p in a["polys"]:
        pcpoly.setdefault(key(na, p), p)
    corner = collections.defaultdict(collections.Counter)
    for q in b["polys"]:
        p = pcpoly.get(key(nb, q))
        if p is None or len(p["normals"]) != len(q["normals"]):
            continue
        ptsa = [na[i] for i in p["pts"]]
        ptsb = [nb[j] for j in q["pts"]]
        off = next(k for k in range(len(ptsa)) if turn(ptsa, k) == ptsb)
        for x, y in zip(q["normals"], turn(p["normals"], off) if len(p["normals"]) > 1 else p["normals"]):
            corner[x][y] += 1

    def psn(j):
        if nob[j] in take:
            return newn["b", j]
        if corner[j]:
            return newn["a", corner[j].most_common(1)[0][0]]
        v = b["normals"][j]
        best = min((i for i in range(len(a["normals"])) if noa[i] == nob[j] and a["normals"][i][3] == v[3]),
                   key=lambda i: sum((a["normals"][i][c] - v[c]) ** 2 for c in range(3)), default=None)
        if best is None:
            raise Fail("body %d: PlayStation normal %d has no PC twin" % (pr.n, j))
        return newn["a", best]

    def pcn(i):
        if noa[i] in take:
            raise Fail("body %d: a PC polygon kept lit by normal %d of a group taken" % (pr.n, i))
        return newn["a", i]

    # the PC's own materials for a colour and a shading
    usual = collections.defaultdict(collections.Counter)
    for p in a["polys"]:
        usual[p["colour"], p["shade"]][p["mat"], p["word"] >> 8] += 1

    polys = []
    for p in a["polys"]:
        if any(oa[i] in take for i in p["pts"]):
            continue
        polys.append((p["mat"], p["word"], [pcpoint(i) for i in p["pts"]], [pcn(i) for i in p["normals"]]))
    for q in b["polys"]:
        if not any(ob[j] in take for j in q["pts"]):
            continue
        out = [j in drop for j in q["pts"]]
        if all(out):
            continue
        if any(out):
            raise Fail("body %d: a polygon across the bun's shape" % pr.n)
        nn = [psn(j) for j in q["normals"]]
        p = pcpoly.get(key(nb, q))
        if p is not None:
            mat, word = p["mat"], p["word"]
        elif usual[q["colour"], q["shade"]]:
            mat, hb = usual[q["colour"], q["shade"]].most_common(1)[0][0]
            word = hb << 8 | q["colour"]
        else:
            mat = {"F": 0, "L": 7, "G": 9}[q["shade"]]
            ds = collections.Counter(normals[m][3] for m in nn)
            word = (4096 // ds.most_common(1)[0][0] if ds else 0) << 8 | q["colour"]
        polys.append((mat, word, [pspoint(j) for j in q["pts"]], nn))

    lines = [(w0, w1, pcpoint(i), pcpoint(j)) for w0, w1, i, j in a["lines"]]
    spheres = [(w0, w1, rad, pcpoint(c)) for s, (w0, w1, rad, c) in enumerate(a["spheres"]) if s not in dropsph]

    # the groups' records, the PC's but for their points, origin and normals
    groups = []
    for k, g in enumerate(a["groups"]):
        raw = bytearray(g["raw"])
        origin = pcpoint(g["origin"]) if k > 0 else g["origin"]
        struct.pack_into("<HHH", raw, 0, 6 * span[k][0], span[k][1], 6 * origin)
        struct.pack_into("<H", raw, 18, ncount[k])
        groups.append(bytes(raw))
    return {"head": a["head"], "points": points, "groups": groups, "normals": normals,
            "polys": polys, "lines": lines, "spheres": spheres, "tail": a["tail"],
            "take": take, "drop": {nb[j] for j in drop}}


def pack(t):
    """The bytes of a body built."""
    d = bytearray(t["head"])
    d += struct.pack("<H", len(t["points"]))
    for p in t["points"]:
        d += struct.pack("<3h", *p)
    d += struct.pack("<H", len(t["groups"]))
    for g in t["groups"]:
        d += g
    d += struct.pack("<H", len(t["normals"]))
    for v in t["normals"]:
        d += struct.pack("<4h", *v)
    d += struct.pack("<H", len(t["polys"]))
    for mat, word, pts, nn in t["polys"]:
        d += struct.pack("<BBH", mat, len(pts), word)
        if mat >= 9:
            for m, i in zip(nn, pts):
                d += struct.pack("<HH", m, 6 * i)
        else:
            if mat >= 7:
                d += struct.pack("<H", nn[0])
            for i in pts:
                d += struct.pack("<H", 6 * i)
    d += struct.pack("<H", len(t["lines"]))
    for w0, w1, i, j in t["lines"]:
        d += struct.pack("<4H", w0, w1, 6 * i, 6 * j)
    d += struct.pack("<H", len(t["spheres"]))
    for w0, w1, rad, c in t["spheres"]:
        d += struct.pack("<4H", w0, w1, rad, 6 * c)
    d += t["tail"]
    return bytes(d)


def check(pr, t, raw):
    """Body raw, read back, against the pair it was made from: every
    reference within it, each corner lit by a normal of its point's
    group, the skeleton the PC's, the groups kept the PC's points and
    normals, the groups taken the PlayStation's, the polygons those of
    their sources, the lines and spheres the PC's.
    Its room in the 1994 renderer: the bytes of List_Entity and the
    entries of List_Tri were every polygon to face the camera."""
    a, b, n = pr.a, pr.b, pr.n
    c = pcbody(raw)
    nc, oc = named(c), owners(c)
    if c["head"] != a["head"] or c["tail"] != a["tail"]:
        raise Fail("body %d: header or tail changed" % n)
    if [g["raw"][6:18] + g["raw"][20:] for g in c["groups"]] != \
            [g["raw"][6:18] + g["raw"][20:] for g in a["groups"]]:
        raise Fail("body %d: groups changed past their points and normals" % n)
    if any(i >= len(c["points"]) for p in c["polys"] for i in p["pts"]) or \
            any(m >= len(c["normals"]) for p in c["polys"] for m in p["normals"]):
        raise Fail("body %d: a polygon past the points or normals" % n)
    if sum(g["count"] for g in c["groups"]) != len(c["points"]) or -1 in oc:
        raise Fail("body %d: points outside the groups" % n)
    if sum(g["ncount"] for g in c["groups"]) != len(c["normals"]):
        raise Fail("body %d: normals outside the groups" % n)
    nown = normalowners(c)
    for p in c["polys"]:
        if p["shade"] == "G" and any(nown[m] != oc[i] for m, i in zip(p["normals"], p["pts"])) or \
                p["shade"] == "L" and nown[p["normals"][0]] not in {oc[i] for i in p["pts"]}:
            raise Fail("body %d: a polygon lit by another group's normal" % n)
    wa, wc = world(a), world(c)
    for k in range(1, len(c["groups"])):
        if wc[c["groups"][k]["origin"]] != wa[a["groups"][k]["origin"]]:
            raise Fail("body %d: group %d moved" % (n, k))
    take = t["take"]
    for k, g in enumerate(c["groups"]):
        src = b if k in take else a
        nm = pr.nb if k in take else pr.na
        sg = src["groups"][k]
        want = collections.Counter(p for p in nm if p[0] == k and p not in t["drop"])
        if collections.Counter(p for p in nc if p[0] == k) != want:
            raise Fail("body %d: group %d's points are not its source's" % (n, k))
        mine = c["normals"][g["nfirst"]:g["nfirst"] + g["ncount"]]
        theirs = src["normals"][sg["nfirst"]:sg["nfirst"] + sg["ncount"]]
        if mine != (list(map(psnormal, theirs)) if k in take else theirs):
            raise Fail("body %d: group %d's normals are not its source's" % (n, k))
    want = collections.Counter(key(pr.na, p) for p in a["polys"] if all(pr.oa[i] not in take for i in p["pts"]))
    left = collections.Counter(key(pr.nb, q) for q in b["polys"]
                               if any(pr.ob[j] in take for j in q["pts"]) and
                               all(pr.nb[j] in t["drop"] for j in q["pts"]))
    want += collections.Counter(key(pr.nb, q) for q in b["polys"] if any(pr.ob[j] in take for j in q["pts"]))
    want -= left
    if collections.Counter(key(nc, p) for p in c["polys"]) != want:
        raise Fail("body %d: the polygons are not their sources'" % n)
    if [(w0, w1, nc[i], nc[j]) for w0, w1, i, j in c["lines"]] != \
            [(w0, w1, pr.na[i], pr.na[j]) for w0, w1, i, j in a["lines"]]:
        raise Fail("body %d: the lines are not the PC's" % n)
    spheres = [(w0, w1, rad, pr.na[s]) for w0, w1, rad, s in a["spheres"]]
    if not set((w0, w1, rad, nc[s]) for w0, w1, rad, s in c["spheres"]) <= set(spheres):
        raise Fail("body %d: the spheres are not the PC's" % n)
    entity = sum(4 + 6 * len(p["pts"]) for p in c["polys"]) + 12 * (len(c["lines"]) + len(c["spheres"]))
    tri = len(c["polys"]) + len(c["lines"]) + len(c["spheres"])
    if len(c["points"]) > NPOINT or len(c["normals"]) > NPOINT or entity > NENTITY or tri > NTRI or \
            max(len(p["pts"]) for p in c["polys"]) > NPOLYPOINT:
        raise Fail("body %d: past the renderer's room" % n)


def fixbodies(hqr, opt, face):
    """Twinsen's bodies, the PlayStation's head or with face its face
    alone on the PC's bodies, each read back and checked."""
    psfile(opt["psdir"], "GAME.HQR")
    psfile(opt["psdir"], "SCENES.BLK")
    files = []
    for name in ("FILE3D.HQR", "SCENE.HQR"):
        path = find(opt["dirs"], name)
        if path is None:
            raise Fail("%s: missing" % name)
        with open(path, "rb") as f:
            data = f.read()
        if form(name, data) is None:
            raise Fail("%s: a form outside the table" % path)
        files.append(Hqr(data).data)
    changed = {}
    for pr in pairs(hqr.data, files[0], files[1], opt["psdir"]):
        t = build(pr, face)
        raw = pack(t)
        check(pr, t, raw)
        changed[pr.n] = raw
    return changed


def fixface(hqr, opt):
    return fixbodies(hqr, opt, True)


def fixhead(hqr, opt):
    return fixbodies(hqr, opt, False)


# Each file and the sets that change it, in the order they apply.
FIX = {
    "SCENE.HQR": (("scenes", fixscenes),),
    "LBA_GRI.HQR": (("grids", fixgrids),),
    "TEXT.HQR": (("texts", fixtexts),),
    "BODY.HQR": (("face", fixface), ("face-full", fixhead)),
}


def find(dirs, name):
    """The path of name in the first of dirs that holds it, its case as
    the directory spells it, or None."""
    for d in dirs:
        try:
            for e in os.listdir(d):
                if e.upper() == name:
                    return os.path.join(d, e)
        except OSError:
            continue
    return None


def form(name, data):
    return FORMS[name].get(hashlib.sha256(data).hexdigest())


def listing(dirs):
    for name in FORMS:
        path = find(dirs, name)
        if path is None:
            print("%s\tmissing" % name)
            continue
        with open(path, "rb") as f:
            data = f.read()
        what = form(name, data) or "unknown"
        print("%s\t%s\t%s\t%s" % (name, path, what, " ".join(set for set, _ in FIX.get(name, ())) or "-"))


def patch(dirs, sets, psdir, outdir):
    """Each file the sets change, found, checked and made, then the set
    written into outdir, each file under a private name first and
    renamed once all are written, so that a refusal or a failure to
    write leaves outdir as it was."""
    opt = {"dirs": dirs, "psdir": psdir}
    outs = []
    for name, fixes in FIX.items():
        fixes = [fix for set, fix in fixes if set in sets]
        if not fixes:
            continue
        path = find(dirs, name)
        if path is None:
            raise Fail("%s: missing" % name)
        with open(path, "rb") as f:
            data = f.read()
        what = form(name, data)
        if what is None:
            raise Fail("%s: a form outside the table" % path)
        if name == "BODY.HQR" and what != "every PC version":
            raise Fail("%s: %s, where the face sets take the PC's bodies alone" % (path, what))
        hqr = Hqr(data)
        changed = {}
        for fix in fixes:
            changed.update(fix(hqr, opt))
            hqr.data = [changed.get(k, d) for k, d in enumerate(hqr.data)]
        out = hqr.write(changed)
        if form(name, out) is None:
            raise Fail("%s: its output's form outside the table" % path)
        if out != data:
            outs.append((name, out))
    for name, _ in outs:
        if os.path.isdir(os.path.join(outdir, name)):
            raise Fail("%s: a directory" % os.path.join(outdir, name))
    tmps = []
    try:
        for name, out in outs:
            tmps.append(os.path.join(outdir, "." + name + ".new"))
            with open(tmps[-1], "wb") as f:
                f.write(out)
        for (name, _), tmp in zip(outs, tmps):
            os.replace(tmp, os.path.join(outdir, name))
    except OSError:
        for tmp in tmps:
            try:
                os.remove(tmp)
            except OSError:
                pass
        raise


def samedir(a, b):
    """Whether a and b name one directory."""
    try:
        return os.path.samefile(a, b)
    except OSError:
        return False


def usage():
    sys.stderr.write("usage: lbapatch.py -l gamedir...\n"
                     "       lbapatch.py [-s set]... [-p psdir] -o outdir gamedir...\n")
    sys.exit(2)


def main(args):
    sets = []
    psdir = None
    outdir = None
    lst = False
    while args and args[0].startswith("-"):
        a = args.pop(0)
        if a == "-l":
            lst = True
        elif a in ("-s", "-p", "-o") and args:
            v = args.pop(0)
            if a == "-s":
                if v not in SETS:
                    sys.stderr.write("lbapatch.py: %s: no such set (%s)\n" % (v, " ".join(SETS)))
                    sys.exit(2)
                sets.append(v)
            elif a == "-p":
                psdir = v
            else:
                outdir = v
        else:
            usage()
    if not args or lst == (outdir is not None) or (lst and (sets or psdir)):
        usage()
    if "face" in sets and "face-full" in sets:
        sys.stderr.write("lbapatch.py: face and face-full exclude each other\n")
        sys.exit(2)
    if ("face" in sets or "face-full" in sets) and psdir is None:
        sys.stderr.write("lbapatch.py: face and face-full need the PlayStation's disc: -p psdir\n")
        sys.exit(2)
    if outdir is not None and any(samedir(outdir, d) for d in args + ([psdir] if psdir else [])):
        sys.stderr.write("lbapatch.py: %s: a directory it reads, whose files the output would replace\n" % outdir)
        sys.exit(2)
    try:
        if lst:
            listing(args)
        else:
            patch(args, sets or DEFAULT, psdir, outdir)
    except (Fail, OSError) as e:
        sys.stderr.write("lbapatch.py: %s\n" % e)
        sys.exit(1)


main(sys.argv[1:])
