"""saves.py: the saves of Little Big Adventure 1 as each version of the
game writes them, read and made again: the model test/lbasave holds
lbasave (tools/lbasave.c) to.

    python3 -I tools/saves.py check [-w] [-g gamedir] [-c corpus] lbasave

A save is the 1994 record (sources/gamemenu.c's SaveGame): its version
byte, the player's name and its NUL, 255 and the game's flags, the
scene, the chapter, the behaviour and the life (a byte each), the gold
(2), the magic level and points and the clover boxes (a byte each),
the entry's X, Y, Z and the heading (2 each), the body (1), 150 and
the holomap's places, the fuel (1), 28 and the inventory, the clovers
(1) and the weapon (2). LBA.EXE, LbaWin and the Classic write version
3, the Classic marking New Game+ in bit 7 of the flags, DotEmu version
4 and its play time, 8 bytes of seconds, LBA1 Legacy version 3 or 16
and its tail: a picture of 160x120, the context of sources/savegame.c's
SaveContexte and the CRC-32 of both, packed by LBA2's LZSS behind its
size when bit 7 of the version byte is set. LBA1 Legacy's earlier forms
are version 16 without the CRC-32, version 17 (the same context in
another order and at the variables' sizes) and the snapshot ("SNAP"
after the record), which lbasave makes today's.

This model reads each form and writes each version's as lbasave must,
on its own: the tables of sources/patch.c are read from its text, the
game's files with hqr.py's reading, and nothing of lbasave's code is
used.

check runs lbasave on saves it makes, every version's and form, and on
the saves in the directory corpus, the one to hold the snapshots of the
oldest form, and holds each answer to the model's: each file read and listed (-l), converted to
every version byte for byte, a packed tail expanded to the model's,
LBA1 Legacy's packed saves packed again to their very bytes, the names
the versions give new saves, the names refused, damaged files refused
or read as their record, each with its line, and the usage refused.
The game's files (-g gamedir) give the snapshots' upgrades and DotEmu's
picture. -w says lbasave is a Windows exe, run under tools/winerun
from the directory of the run, every path given to it relative. It
prints a line for each difference and exits 1 if there was one.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

NNAME = 29
NFLAGS, NHOLO, NINV = 255, 150, 28
MARK = PACKED = 0x80
VPC, VDOTEMU, VPORT, VLBA2 = 3, 4, 16, 17
NPIC = 160 * 120
NTAIL = 1 << 20
NCUBE = 64 * 25 * 64 * 2
NOBJ, NZONES, NEXTRAS, NINCRUST = 100, 255, 50, 10
EXTRA, INCRUST, ACTOR, BLOCK, VARS, OWN = 34, 16, 147, 105, 2798, 792
SPRITE3D = 1024
KEYNONE, KEYBLEND = -1, -2

# version: version byte, longest name, keeps New Game+, writes the
# time, writes the tail, names by the date, refuses a name another save
# of the directory has
VERSIONS = {
    'lba': (VPC, 29, False, False, False, False, True),
    'lbawin': (VPC, 28, False, False, False, False, True),
    'classic': (VPC, 29, True, False, False, False, True),
    'dotemu': (VDOTEMU, 28, False, True, False, True, False),
    'legacy': (VPC, 29, False, False, True, False, True),
}

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class NoSave(Exception):
    pass


def u16(b, p):
    return struct.unpack_from('<H', b, p)[0]


def s16(b, p):
    return struct.unpack_from('<h', b, p)[0]


def u32(b, p):
    return struct.unpack_from('<I', b, p)[0]


def s32(b, p):
    return struct.unpack_from('<i', b, p)[0]


def crc(b):
    return zlib.crc32(bytes(b)) & 0xffffffff


def expand(src, size, exact=True):
    """LZS expanded to size, as the port's ExpandSave checks it, or None
    for a stream that runs out, copies from before its start or, when
    exact, holds bytes past its last."""
    d = bytearray()
    s = 0
    while len(d) < size:
        if s >= len(src):
            return None
        flags = src[s]
        s += 1
        for _ in range(8):
            if len(d) >= size:
                break
            if flags & 1:
                if s >= len(src):
                    return None
                d.append(src[s])
                s += 1
            else:
                if len(src) - s < 2:
                    return None
                w = src[s] | src[s + 1] << 8
                s += 2
                n, back = (w & 0xF) + 2, (w >> 4) + 1
                if back > len(d) or n > size - len(d):
                    return None
                for _ in range(n):
                    d.append(d[-back])
            flags >>= 1
    if exact and s != len(src):
        return None
    return bytes(d)


def pack(t):
    """An LZS stream of t that the port reads: runs of a byte copied
    from the one before, the rest as it is."""
    out = bytearray()
    i = 0
    while i < len(t):
        flags, items = 0, bytearray()
        for k in range(8):
            if i >= len(t):
                break
            n = 0
            while i > 0 and i + n < len(t) and n < 17 and t[i + n] == t[i - 1]:
                n += 1
            if n >= 2:
                items += struct.pack('<H', n - 2)
                i += n
            else:
                flags |= 1 << k
                items.append(t[i])
                i += 1
        out.append(flags)
        out += items
    return bytes(out)


def record(b):
    """The record's name end (its NUL) and end, or NoSave."""
    if len(b) < 2:
        raise NoSave('cut short')
    try:
        name = b.index(0, 1)
    except ValueError:
        raise NoSave('its name has no end')
    if name > NNAME + 1:
        raise NoSave('its name runs past 29 characters')
    p = name + 1
    for count, n in ((NFLAGS, 0), (0, 18), (NHOLO, 0), (0, 1), (NINV, 0), (0, 3)):
        if count:
            if p >= len(b) or b[p] != count:
                raise NoSave('a count is another')
            p += 1 + count
        else:
            p += n
        if p > len(b):
            raise NoSave('cut short')
    return name, p


class Game:
    """The game's files and sources/patch.c's tables."""

    def __init__(self, gamedir):
        self.dir = gamedir
        text = open(os.path.join(REPO, 'sources', 'patch.c')).read()
        tab = text[text.index('TabPatches[] = {'):]
        tab = tab[:tab.index('};')]
        self.tab = [(int(a), int(b)) for a, b in re.findall(r'\{(\d+), (\d+)\}', tab)]
        index = text[text.index('IndexPatches[] = {'):]
        index = index[index.index('{') + 1:index.index('};')]
        self.index = [int(x) for x in re.findall(r'\d+', index)]
        self.cache = {}

    def places(self, scene):
        """The places scene's scripts rewrite: (size, offset)s."""
        if scene < 0 or scene >= len(self.index) - 1:
            return []
        return self.tab[self.index[scene]:self.index[scene + 1]]

    def entry(self, name, k):
        """Entry k of the game's HQR file name, expanded, or None."""
        if self.dir is None:
            return None
        if (name, k) in self.cache:
            return self.cache[(name, k)]
        path = None
        for f in os.listdir(self.dir):
            if f.lower() == name.lower():
                path = os.path.join(self.dir, f)
        e = None
        if path is not None:
            d = open(path, 'rb').read()
            if 0 <= k < u32(d, 0) // 4 and u32(d, 4 * k):
                off = u32(d, 4 * k)
                size, packed, method = struct.unpack_from('<IIH', d, off)
                raw = d[off + 10:off + 10 + packed]
                e = raw[:size] if method == 0 else expand(raw, size, False)
        self.cache[(name, k)] = e
        return e


def walk(c, scene, game):
    """Today's context read through as LoadContexte reads it: a dict of
    what it holds, or NoSave."""
    if len(c) < 160:
        raise NoSave('cut short')
    w = {'timer': u32(c, 93), 'checksum': u32(c, 101), 'nobj': s32(c, 156)}
    if not 1 <= w['nobj'] <= NOBJ:
        raise NoSave('actors')
    p = 160
    w['hero'] = struct.unpack_from('<iii', c, p + 98)
    for _ in range(w['nobj']):
        if len(c) - p < ACTOR or c[p + 114] > 1 or s32(c, p + 119) < KEYBLEND:
            raise NoSave('an actor')
        p += ACTOR
    pl = game.places(scene)
    if len(c) - p < 8 or s32(c, p) != len(pl) or u32(c, p + 4) != sum(s for s, o in pl):
        raise NoSave('patches')
    w['npatch'] = len(pl)
    p += 8 + u32(c, p + 4)
    if len(c) - p < 1 + NEXTRAS * EXTRA + 4 or c[p] != NEXTRAS:
        raise NoSave('extras')
    p += 1 + NEXTRAS * EXTRA
    w['nzones'] = s32(c, p)
    if not 0 <= w['nzones'] <= NZONES:
        raise NoSave('zones')
    p += 4 + 12 * w['nzones']
    if len(c) - p < 1 or c[p] > NINCRUST:
        raise NoSave('overlays')
    w['nincrust'] = c[p]
    p += 1 + INCRUST * c[p] + 12 + OWN
    w['runs'] = 0
    while True:
        if len(c) - p < 4:
            raise NoSave('grid')
        off = u32(c, p)
        p += 4
        if off == NCUBE:
            break
        if len(c) - p < 4:
            raise NoSave('grid')
        n = u32(c, p)
        if off > NCUBE or n > NCUBE - off or len(c) - p - 4 < n:
            raise NoSave('grid')
        p += 4 + n
        w['runs'] += 1
    if len(c) - p < 1:
        raise NoSave('samples')
    w['loops'] = c[p]
    if len(c) - p - 1 != 12 * c[p] + 4:
        raise NoSave('end')
    return w


class Out:
    """Bytes as SaveContexte writes them, its widths checked."""

    def __init__(self):
        self.b = bytearray()
        self.cut = False

    def byte(self, v):
        if not 0 <= v <= 255:
            self.cut = True
        self.b.append(v & 0xff)

    def word(self, v):
        if not -32768 <= v <= 65535:
            self.cut = True
        self.b += struct.pack('<H', v & 0xffff)

    def long(self, v):
        self.b += struct.pack('<I', v & 0xffffffff)

    def raw(self, v):
        self.b += v


def actor(o, a, has, key, keytime):
    """An actor of the earlier forms' 105 bytes at a, written as
    today's."""
    o.byte(a[0])
    o.byte(a[3])
    o.word(a[1])
    o.word(a[2])
    for k in (22, 24, 26, 60, 62, 64, 66):
        o.long(s16(a, k))
    o.word(s16(a, 82))
    o.byte(s16(a, 84))
    for k in (86, 8, 4, 6):
        o.word(s16(a, k))
    o.byte(s16(a, 12))
    for k in (28, 30, 32, 34, 36, 38):
        o.word(s16(a, k))
    o.raw(a[44:54])
    for k in (56, 42, 58, 70):
        o.word(s16(a, k))
    o.byte(s16(a, 54))
    for k in (68, 72, 74, 76):
        o.word(s16(a, k))
    o.long(s16(a, 78))
    o.long(s16(a, 80))
    o.word(s16(a, 96))
    o.word(s16(a, 102))
    o.byte(a[104])
    for k in (16, 18, 20, 40):
        o.long(s16(a, k))
    o.byte(has)
    o.long(s16(a, 98))
    o.long(key)
    o.long(keytime)
    for k in (100, 90, 92, 94, 88):
        o.long(s16(a, k))


def runs(c, p):
    """The end of the grid's runs at c[p:], or NoSave."""
    while True:
        if len(c) - p < 4:
            raise NoSave('grid')
        off = u32(c, p)
        p += 4
        if off == NCUBE:
            return p
        n = u32(c, p) if len(c) - p >= 4 else -1
        if n < 0 or off > NCUBE or n > NCUBE - off or len(c) - p - 4 < n:
            raise NoSave('grid')
        p += 4 + n


def loops(c, p):
    if len(c) - p < 1 or len(c) - p < 1 + 12 * c[p]:
        raise NoSave('samples')
    return p + 1 + 12 * c[p]


def from17(t, scene, game):
    """The version 17's tail, made today's (without its CRC-32)."""
    c = t[NPIC:]
    o = Out()
    o.raw(t[:NPIC])
    if len(c) < 166:
        raise NoSave('cut short')
    o.raw(c[:80])
    o.byte(s16(c, 80))
    for k in (82, 86, 90):
        o.long(u32(c, k))
    o.long(u32(c, 106))
    o.byte(s16(c, 110))
    o.byte(s16(c, 112))
    o.word(s16(c, 114))
    o.long(u32(c, 120))
    for k in (124, 126, 128):
        o.long(s16(c, k))
    o.byte(s16(c, 130))
    o.byte(s16(c, 132))
    o.long(-1)
    o.long(s16(c, 134))
    o.byte(s16(c, 136))
    o.word(s16(c, 138))
    o.byte(s16(c, 140))
    o.long(s16(c, 142))
    o.raw(c[144:154])
    o.long(s16(c, 154))
    o.byte(s16(c, 156))
    o.long(s16(c, 158))
    o.word(s16(c, 160))
    nobj = s32(c, 162)
    if not 1 <= nobj <= NOBJ:
        raise NoSave('actors')
    o.long(nobj)
    p = 166
    for _ in range(nobj):
        if len(c) - p < BLOCK + 1:
            raise NoSave('cut short')
        a = c[p:p + BLOCK]
        has = c[p + BLOCK]
        p += BLOCK + 1
        key, keytime = KEYNONE, 0
        if has > 1:
            raise NoSave('an actor')
        if has and not s16(a, 78) & SPRITE3D:
            if len(c) - p < 6:
                raise NoSave('cut short')
            key, keytime = s16(c, p), u32(c, p + 2)
            p += 6
            if key < KEYBLEND:
                raise NoSave('an actor')
        actor(o, a, has, key, keytime)
    pl = game.places(scene)
    if len(c) - p < 8 or s32(c, p) != len(pl) or u32(c, p + 4) != sum(s for s, x in pl):
        raise NoSave('patches')
    size = u32(c, p + 4)
    if len(c) - p - 8 < size + NEXTRAS * EXTRA + 2:
        raise NoSave('cut short')
    o.raw(c[p:p + 8 + size])
    p += 8 + size
    o.byte(NEXTRAS)
    o.raw(c[p:p + NEXTRAS * EXTRA])
    p += NEXTRAS * EXTRA
    nz = s16(c, p)
    p += 2
    if not 0 <= nz <= NZONES or len(c) - p < 8 * nz + 1:
        raise NoSave('zones')
    o.long(nz)
    for _ in range(nz):
        for k in (2, 4, 6):
            o.long(s16(c, p + k))
        p += 8
    if c[p] > NINCRUST or len(c) - p < 1 + INCRUST * c[p] + OWN:
        raise NoSave('overlays')
    o.raw(c[p:p + 1 + INCRUST * c[p]])
    p += 1 + INCRUST * c[p]
    for k in (94, 98, 102):
        o.long(u32(c, k))
    o.raw(c[p:p + OWN])
    p += OWN
    q = loops(c, runs(c, p))
    if len(c) - q != 4:
        raise NoSave('end')
    o.raw(c[p:])
    if o.cut:
        raise NoSave('a value is out of its width')
    return bytes(o.b)


def keyframe(game, anim, off):
    """The keyframe of animation anim at off, as KeyFrame finds it."""
    if anim < 0:
        return KEYBLEND
    a = game.entry('ANIM.HQR', anim)
    if a is None:
        raise NoSave('ANIM.HQR')
    if len(a) < 8:
        return KEYBLEND
    step = 8 * u16(a, 2) + 8
    if off >= 8 and off - 8 < len(a) - 8 and (off - 8) % step == 0 \
            and (off - 8) // step < u16(a, 0):
        return (off - 8) // step
    return KEYBLEND


def zones(s):
    """A scene's zones, walked as LoadScene walks it: count, offset."""
    p = 45
    for _ in range(2):
        p += 2 + u16(s, p)
    n = u16(s, p)
    p += 2
    for _ in range(1, n):
        p += 35
        for _ in range(2):
            p += 2 + u16(s, p)
    return u16(s, p), p + 2


def fromsnap(b, scene, game):
    """A snapshot, made today's tail (without its CRC-32)."""
    if u16(b, 0) != 1 or u16(b, 2) != scene:
        raise NoSave('another layout or scene')
    size, nobj = u32(b, 4), u16(b, 12)
    s = b[22:22 + size]
    v = b[22 + size:22 + size + VARS]
    hq = game.entry('SCENE.HQR', scene)
    if hq is None or len(hq) != size or crc(hq) != u32(b, 8):
        raise NoSave('SCENE.HQR')
    pl = game.places(scene)
    nz, z = zones(s)
    keep = set()
    for n, off in pl:
        keep.update(range(off, off + n))
    for k in range(nz):
        keep.update(range(z + 24 * k + 18, z + 24 * k + 24))
    if any(s[i] != hq[i] and i not in keep for i in range(size)):
        raise NoSave('the scene differs')
    o = Out()
    o.raw(bytes(NPIC))
    o.raw(v[:80])
    o.byte(s16(v, 80))
    for k in (128, 132, 136):
        o.long(u32(v, k))
    o.long(u32(b, 14))
    o.byte(s16(v, 124))
    o.byte(s16(v, 92))
    o.word(s16(v, 94))
    o.long(u32(b, 8))
    for k in (122, 120, 118):
        o.long(s16(v, k))
    o.byte(s16(v, 84))
    o.byte(s16(v, 116))
    o.long(-1)
    o.long(s16(v, 86))
    o.byte(s16(v, 88))
    o.word(s16(v, 90))
    o.byte(s16(v, 110))
    o.long(s16(v, 96))
    o.raw(v[98:108])
    o.long(s16(v, 108))
    o.byte(s16(v, 126))
    o.long(0)
    o.word(s16(v, 114))
    o.long(nobj)
    p = 22 + size + VARS
    for _ in range(nobj):
        a = b[p:p + BLOCK]
        kind = b[p + BLOCK + 4]
        p += BLOCK + 5
        has, key, keytime = 0, KEYNONE, 0
        if kind == 2:
            has = 1 if s16(b, p) != -1 else 0
            p += 2
        elif kind == 1:
            where, off, keytime, g = b[p], u32(b, p + 1), u32(b, p + 5), u32(b, p + 9)
            p += 13 + g
            has = 1
            key = KEYNONE if where == 0 else keyframe(game, s16(a, 98), off) if where == 2 else KEYBLEND
        elif kind != 0:
            raise NoSave('an actor')
        actor(o, a, has, key, keytime)
    o.long(len(pl))
    o.long(sum(n for n, off in pl))
    for n, off in pl:
        o.raw(s[off:off + n])
    o.byte(NEXTRAS)
    o.raw(v[938:938 + NEXTRAS * EXTRA])
    o.long(nz)
    for k in range(nz):
        for j in (18, 20, 22):
            o.long(s16(s, z + 24 * k + j))
    live = [v[2638 + INCRUST * k:2638 + INCRUST * (k + 1)] for k in range(NINCRUST)
            if s16(v, 2638 + INCRUST * k) != -1]
    o.byte(len(live))
    for d in live:
        o.raw(d)
    for k in (140, 144, 148):
        o.long(u32(v, k))
    for k in (82, 112, 934, 936, 152, 154, 156):
        o.word(s16(v, k))
    o.raw(v[158:158 + 768])
    o.word(s16(v, 926))
    o.long(u32(v, 928))
    o.word(s16(v, 932))
    o.word(-1)
    p += 4 + 5000
    q = runs(b, p)
    o.raw(b[p:q])
    q += 4
    r = loops(b, q)
    o.raw(b[q:r])
    if len(b) - 4 - r != 0:
        raise NoSave('end')
    o.raw(b[18:22])
    if o.cut:
        raise NoSave('a value is out of its width')
    return bytes(o.b)


class Save:
    """A save read as lbasave reads it."""

    def __init__(self, b, game):
        self.b = b
        self.name, self.rec = record(b)
        self.scene = b[self.name + 2 + NFLAGS]
        self.marks = any(x & MARK for x in b[self.name + 2:self.name + 2 + NFLAGS])
        self.version = b[0]
        if self.version in (VPORT | PACKED, VLBA2 | PACKED):
            self.version &= ~PACKED
        after = b[self.rec:]
        self.time = None
        self.extra = 0
        self.form = self.tail = self.why = self.snap = None
        if self.version == VPC:
            self.extra = len(after)
        elif self.version == VDOTEMU:
            self.time = int.from_bytes(after[:8], 'little')
            self.extra = max(len(after) - 8, 0)
        elif self.version in (VPORT, VLBA2):
            self.tailof(after, game)
        else:
            self.extra = len(after)

    def tailof(self, c, game):
        if self.version == VPORT and not self.b[0] & PACKED and len(c) >= 8 and c[:4] == b'SNAP':
            self.form = 'snapshot'
            n = u32(c, 4)
            if n < 4 or n != len(c) - 8:
                self.why = 'size'
            elif crc(c[8:8 + n - 4]) != u32(c, 8 + n - 4):
                self.why = 'crc'
            else:
                self.snap = c[8:8 + n]
            return
        self.form = '16' if self.version == VPORT else '17'
        if self.b[0] & PACKED:
            size = u32(c, 0) if len(c) >= 4 else 0
            if not NPIC < size <= NTAIL or len(c) - 4 >= size:
                self.why = 'size'
                return
            t = expand(c[4:], size)
            if t is None:
                self.why = 'stream'
                return
        else:
            if not NPIC < len(c) < NTAIL:
                self.why = 'size'
                return
            t = c
        try:
            if self.version == VLBA2:
                t = from17(t, self.scene, game)
                self.tail = t + struct.pack('<I', crc(t))
            elif len(t) > NPIC + 4 and crc(t[:-4]) == u32(t, len(t) - 4):
                walk(t[NPIC:-4], self.scene, game)
                self.tail = t
            else:
                walk(t[NPIC:], self.scene, game)
                self.form = '16 before its CRC-32'
                self.tail = t + struct.pack('<I', crc(t))
        except NoSave as e:
            self.why = str(e)

    def prepare(self, game):
        if self.snap is not None and self.tail is None:
            t = fromsnap(self.snap, self.scene, game)
            self.tail = t + struct.pack('<I', crc(t))

    def convert(self, version, name, time=None):
        """The bytes version writes, the tail unpacked, and what it
        drops: a set of 'marks', 'time', 'scene', 'damaged', 'extra'."""
        vb, maxname, marks, wtime, wtail, dated, names = VERSIONS[version]
        own = wtail and self.tail is not None
        out = bytearray([VPORT if own else vb])
        out += name + b'\0'
        rest = bytearray(self.b[self.name + 1:self.rec])
        if not marks:
            for i in range(1, 1 + NFLAGS):
                rest[i] &= ~MARK
        out += rest
        if wtime:
            t = self.time if time is None else time
            out += (t or 0).to_bytes(8, 'little', signed=True)
        if own:
            out += self.tail
        drops = set()
        if self.marks and not marks:
            drops.add('marks')
        if self.time and not wtime:
            drops.add('time')
        if (self.tail is not None or self.snap is not None) and not wtail:
            drops.add('scene')
        if self.why is not None:
            drops.add('damaged')
        if self.extra:
            drops.add('extra')
        return bytes(out), drops


def picture(tail, pal):
    """DotEmu's picture of a slot: the tail's picture doubled, RGB."""
    rows = []
    for y in range(240):
        row = bytearray()
        for x in range(320):
            c = tail[(y // 2) * 160 + x // 2]
            row += pal[3 * c:3 * c + 3]
        rows.append(bytes(row))
    return rows


def png(path):
    """A PNG's rows of RGB, read as lbasave writes it, or None."""
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    p, idat, w, h = 8, b'', 0, 0
    while p < len(d):
        n = struct.unpack_from('>I', d, p)[0]
        kind = d[p + 4:p + 8]
        data = d[p + 8:p + 8 + n]
        if struct.unpack_from('>I', d, p + 8 + n)[0] != zlib.crc32(kind + data) & 0xffffffff:
            return None
        if kind == b'IHDR':
            w, h = struct.unpack_from('>II', data, 0)
            if data[8:13] != b'\x08\x02\x00\x00\x00':
                return None
        elif kind == b'IDAT':
            idat += data
        p += 12 + n
    raw = zlib.decompress(idat)
    rows = []
    for y in range(h):
        line = raw[y * (1 + 3 * w):(y + 1) * (1 + 3 * w)]
        if line[0] != 0:
            return None
        rows.append(line[1:])
    return rows


class Check:
    """lbasave held to the model."""

    def __init__(self, tool, windows, game, work):
        self.tool = tool
        self.windows = windows
        self.game = game
        self.work = work
        self.fails = 0
        self.runs = 0

    def fail(self, msg):
        self.fails += 1
        print('saves.py: %s' % msg)

    def run(self, *args):
        """lbasave run on args in the work directory: status, stdout,
        stderr lines."""
        self.runs += 1
        cmd = [self.tool] + list(args)
        if self.windows:
            cmd = [os.path.join(REPO, 'tools', 'winerun'), '-q', self.tool] + list(args)
        p = subprocess.run(cmd, cwd=self.work, capture_output=True)
        err = [l for l in p.stderr.decode('latin-1').splitlines() if l.startswith('lbasave')]
        return p.returncode, p.stdout.decode('latin-1').splitlines(), err

    def path(self, *names):
        return os.path.join(self.work, *names)

    def put(self, name, b, mtime=None):
        os.makedirs(os.path.dirname(self.path(name)) or self.work, exist_ok=True)
        with open(self.path(name), 'wb') as f:
            f.write(b)
        if mtime is not None:
            os.utime(self.path(name), (mtime, mtime))

    def get(self, name):
        with open(self.path(name), 'rb') as f:
            return f.read()

    def fresh(self, name):
        shutil.rmtree(self.path(name), ignore_errors=True)
        os.makedirs(self.path(name))

    def compare(self, what, got, want, packed_ok):
        """got, a file lbasave wrote, against want, the model's bytes
        with the tail unpacked. A packed tail expanded first."""
        if got == want:
            return
        if packed_ok and len(got) > 4 and got[0] == want[0] | PACKED:
            s = Save(want, self.game)
            rec = s.rec
            if got[1:rec] == want[1:rec]:
                size = u32(got, rec)
                t = expand(got[rec + 4:], size)
                if t is not None and t == want[rec:] and len(got) - rec - 4 < size:
                    return
        self.fail('%s: %d bytes differ from the model\'s %d' % (what, len(got), len(want)))

    def notes(self, what, err, drops):
        """lbasave's lines against the drops the model expects."""
        kinds = {
            'marks': 'New Game+ dropped',
            'time': 'the play time dropped',
            'scene': 'the scene as it ran dropped: the save',
            'damaged': 'the scene as it ran dropped, ',
            'extra': 'bytes after the record dropped',
        }
        for k, text in kinds.items():
            if (k in drops) != any(text in l for l in err):
                self.fail('%s: %s line %s' % (what, k, 'missing' if k in drops else 'unexpected'))

    def convert(self, what, src, b, version, name=None, out=None, time=None):
        """src (written with bytes b) converted to version into the file
        out, held to the model."""
        s = Save(b, self.game)
        if VERSIONS[version][4]:
            s.prepare(self.game)
        outbase = os.path.basename(out)
        who = name if name is not None else b[1:s.name]
        if VERSIONS[version][4] and outbase.lower() in ('autosave.lba', 'current.lba'):
            who = b'AUTOSAVE' if outbase.lower() == 'autosave.lba' else b'CURRENT'
        want, drops = s.convert(version, who, time)
        args = ['-t', version]
        if name is not None:
            args += ['-n', name.decode('latin-1')]
        if time is not None:
            args += ['-T', str(time)]
        if self.game.dir is not None:
            args += ['-g', self.game.dir]
        st, lines, err = self.run(*(args + [src, out]))
        if st != 0 or lines != [out]:
            self.fail('%s to %s: status %d, %s' % (what, version, st, err[:2]))
            return
        own = VERSIONS[version][4] and s.tail is not None and outbase.lower() not in ('autosave.lba', 'current.lba')
        got = self.get(out)
        self.compare('%s to %s' % (what, version), got, want, own)
        if own and len(pack(s.tail)) < len(s.tail) * 9 // 10 and not got[0] & PACKED:
            self.fail('%s to %s: a tail that packs written unpacked' % (what, version))
        self.notes('%s to %s' % (what, version), err, drops)
        return s

    def listing(self, what, src, b):
        """-l's fields against the model's reading."""
        st, lines, err = self.run('-l', src)
        try:
            s = Save(b, self.game)
        except NoSave:
            if st != 1 or not err:
                self.fail('%s: listed, the model reads no save' % what)
            return
        if st != 0:
            self.fail('%s: not listed: %s' % (what, err[:1]))
            return
        f = dict(l.split('\t', 1) for l in lines if '\t' in l)
        r = s.name + 2 + NFLAGS
        inv = r + 18 + 1 + NHOLO + 1 + 1
        want = {
            'name': b[1:s.name].decode('latin-1'),
            'scene': str(b[r]),
            'chapter': str(b[r + 1]),
            'behaviour': str(b[r + 2]),
            'life': str(b[r + 3]),
            'gold': str(u16(b, r + 4)),
            'magic': '%d %d' % (b[r + 6], b[r + 7]),
            'cloverboxes': str(b[r + 8]),
            'entry': '%d %d %d' % (s16(b, r + 9), s16(b, r + 11), s16(b, r + 13)),
            'beta': str(s16(b, r + 15)),
            'body': str(b[r + 17]),
            'fuel': str(b[inv - 2]),
            'clovers': str(b[inv + NINV]),
            'weapon': str(s16(b, inv + NINV + 1)),
            'flags': ' '.join('%d=%d' % (i, b[s.name + 2 + i]) for i in range(NFLAGS) if b[s.name + 2 + i]),
            'inventory': ' '.join(str(i) for i in range(NINV) if b[inv + i]),
            'newgameplus': 'yes' if s.marks else 'no',
        }
        if s.time is not None:
            want['time'] = str(s.time)
        if s.form is not None:
            want['tail'] = s.form
        for k, v in want.items():
            if f.get(k) != v:
                self.fail('%s: -l %s %r, the model %r' % (what, k, f.get(k), v))


def fixed(scene=1, chapter=2):
    """A record's fields from the scene to the body, each its own."""
    return struct.pack('<BBBBHBBBhhhhB', scene, chapter, 1, 37, 1234, 2, 40, 5, 1000, 512, 2000, 300, 0)


def rec(name=b'abc', version=VPC, flags=None, scene=1, chapter=2):
    """A record of the 1994 game's form."""
    if flags is None:
        flags = bytes((i * 7) % 2 for i in range(NFLAGS))
    b = bytearray([version]) + name + b'\0'
    b += bytes([NFLAGS]) + flags
    b += fixed(scene, chapter)
    b += bytes([NHOLO]) + bytes((i * 3) % 5 for i in range(NHOLO))
    b += bytes([9])
    b += bytes([NINV]) + bytes(i % 2 for i in range(NINV))
    b += struct.pack('<Bh', 3, 1)
    return bytes(b)


def context16(game, scene, nobj=2):
    """Today's context for scene, each count used."""
    o = Out()
    o.raw(bytes((i * 5) & 0xff for i in range(80)))
    o.byte(3)
    for v in (10, 3, 12, 1234, ):
        o.long(v)
    o.byte(0)
    o.byte(1)
    o.word(77)
    o.long(0x12345678)
    for v in (4, 2, 1):
        o.long(v)
    o.byte(1)
    o.byte(0)
    o.long(-1)
    o.long(-1)
    o.byte(1)
    o.word(3)
    o.byte(0)
    o.long(512)
    o.raw(bytes(range(10)))
    o.long(1)
    o.byte(0)
    o.long(2)
    o.word(-1)
    o.long(nobj)
    for k in range(nobj):
        a = bytearray((k * 13 + i) & 0x7f for i in range(ACTOR))
        a[114] = 1
        struct.pack_into('<i', a, 119, k - 1)
        o.raw(bytes(a))
    pl = game.places(scene)
    o.long(len(pl))
    o.long(sum(n for n, off in pl))
    for n, off in pl:
        o.raw(bytes((off + j) & 0xff for j in range(n)))
    o.byte(NEXTRAS)
    o.raw(bytes((i * 11) & 0xff for i in range(NEXTRAS * EXTRA)))
    o.long(2)
    o.raw(bytes(range(24)))
    o.byte(1)
    o.raw(bytes(range(40, 56)))
    o.raw(bytes(range(12)))
    o.raw(bytes((i * 3) & 0xff for i in range(OWN)))
    o.long(100)
    o.long(5)
    o.raw(b'\x01\x02\x03\x04\x05')
    o.long(5000)
    o.long(3)
    o.raw(b'\x09\x08\x07')
    o.long(NCUBE)
    o.byte(1)
    o.raw(bytes(range(12)))
    o.long(0xCAFEF00D)
    return bytes(o.b)


def block(k):
    """An actor's 105 bytes of the earlier forms, each field its own,
    those today's keeps in a byte within one."""
    a = bytearray((k * 13 + i) & 0x7f for i in range(BLOCK))
    for at, v in ((12, 2), (54, 1), (84, 5)):
        struct.pack_into('<h', a, at, v)
    return a


def context17(game, scene, nobj=2):
    """The version 17's context for scene, each count used."""
    c = bytearray()
    c += bytes((i * 5) & 0xff for i in range(80))
    c += struct.pack('<h', 3)
    c += struct.pack('<iii', 10, 3, 12)
    c += struct.pack('<iii', 5120, 768, 6144)
    c += struct.pack('<I', 1234)
    c += struct.pack('<hhh', 0, 1, 77)
    c += struct.pack('<II', 999, 0x12345678)
    c += struct.pack('<hhhhhhhhhh', 4, 2, 1, 1, 0, -1, 1, 3, 0, 512)
    c += bytes(range(10))
    c += struct.pack('<hhhh', 1, 0, 2, -1)
    c += struct.pack('<i', nobj)
    for k in range(nobj):
        a = block(k)
        struct.pack_into('<h', a, 78, SPRITE3D if k else 0)
        c += a
        c.append(1)
        if not k:
            c += struct.pack('<hI', 2, 777)
    pl = game.places(scene)
    c += struct.pack('<iI', len(pl), sum(n for n, off in pl))
    for n, off in pl:
        c += bytes((off + j) & 0xff for j in range(n))
    c += bytes((i * 11) & 0xff for i in range(NEXTRAS * EXTRA))
    c += struct.pack('<h', 2) + bytes(range(16))
    c += bytes([1]) + bytes(range(40, 56))
    c += bytes((i * 3) & 0xff for i in range(OWN))
    c += struct.pack('<II', 100, 5) + b'\x01\x02\x03\x04\x05'
    c += struct.pack('<I', NCUBE)
    c += bytes([1]) + bytes(range(12))
    c += struct.pack('<I', 0xCAFEF00D)
    return bytes(c)


def picture16():
    """A picture of runs of 16 pixels, which pack packs."""
    return bytes(((i // 16) * 7 + i // 160) & 0xff for i in range(NPIC))


def synthetic(chk):
    """Saves of every version and form, made here, through lbasave."""
    g = chk.game
    chk.fresh('syn')
    base = rec()
    tail = picture16() + context16(g, 1)
    tail += struct.pack('<I', crc(tail))
    old = picture16() + context16(g, 1)
    t17 = picture16() + context17(g, 1)
    ngp = bytearray(b'\0' * NFLAGS)
    for k in (0, 1, 2, 4, 6, 12, 15):
        ngp[k] = MARK
    ngp[14] = 1
    files = {
        'S12.LBA': base,
        'name29.LBA': rec(b'A' * 29),
        'name28.LBA': rec(b'B' * 28),
        'empty.LBA': rec(b''),
        'cp437.LBA': rec(b'Fran\x87ois'),
        'S9271.LBA': rec(b'ngp', flags=bytes(ngp), scene=0, chapter=0),
        '20200509161619.LBA': rec(b'Twinsen', version=VDOTEMU) + struct.pack('<q', 4464),
        'quick.LBA_quick.LBA': rec(b'Twinsen', version=VDOTEMU) + struct.pack('<q', 4510),
        '20150617171334.LBA': rec(b'Twinsen', version=VDOTEMU) + struct.pack('<I', 0),
        'S2075.LBA': rec(b'dor', version=VPORT | PACKED) + struct.pack('<I', len(tail)) + pack(tail),
        'current.lba': rec(b'CURRENT', version=VPORT) + tail,
        'autosave.lba': rec(b'AUTOSAVE', version=VPORT) + tail,
        'S3000.LBA': rec(b'old', version=VPORT) + old,
        'S3001.LBA': rec(b'oldp', version=VPORT | PACKED) + struct.pack('<I', len(old)) + pack(old),
        'S5534.LBA': rec(b'Saut', version=VLBA2 | PACKED) + struct.pack('<I', len(t17)) + pack(t17),
        'S5535.LBA': rec(b'Sau2', version=VLBA2) + t17,
        'S4000.LBA': rec(b'dmg', version=VPORT) + tail,
        'trail.LBA': base + b'XYZ',
        'weird.LBA': rec(b'odd', version=0x42) + b'??',
        'SNAP.LBA': rec(b'snp', version=VPORT) + b'SNAP' + struct.pack('<I', 8) + b'\0' * 8,
    }
    for name, b in files.items():
        chk.put(os.path.join('syn', name), b, mtime=1600000000)
    for name in ('S2075.LBA', 'current.lba', 'autosave.lba', 'S3000.LBA', 'S3001.LBA', 'S5534.LBA',
                 'S5535.LBA', 'S4000.LBA'):
        s = Save(files[name], g)
        if s.tail is None or s.why is not None:
            chk.fail('the synthetic %s reads as no tail of the model\'s: %s' % (name, s.why))
    return files


def damaged(chk, base, tailed):
    """Damaged saves: refused, or read as their record with a line."""
    chk.fresh('bad')
    _, end = record(base)
    for n in range(end):
        chk.put('bad/cut.LBA', base[:n])
        st, lines, err = chk.run('-t', 'lba', 'bad/cut.LBA', 'bad/out.LBA')
        if st != 1 or lines or not err:
            chk.fail('a record cut at %d: status %d, %s' % (n, st, lines))
    for at, value in ((5, 254), (5 + 1 + NFLAGS + 18, 151), (5 + 1 + NFLAGS + 18 + 1 + NHOLO + 1, 27)):
        b = bytearray(base)
        b[at] = value
        chk.put('bad/count.LBA', bytes(b))
        st, lines, err = chk.run('-t', 'lba', 'bad/count.LBA', 'bad/out.LBA')
        if st != 1 or lines:
            chk.fail('a count of %d: status %d' % (value, st))
    chk.put('bad/name.LBA', bytes([VPC]) + b'N' * 40 + base[4:])
    st, lines, err = chk.run('-t', 'lba', 'bad/name.LBA', 'bad/out.LBA')
    if st != 1:
        chk.fail('a name of 40: status %d' % st)
    _, end = record(tailed)
    for what, b in (
            ('a tail\'s byte changed', tailed[:end + 200] + bytes([tailed[end + 200] ^ 1]) + tailed[end + 201:]),
            ('a tail cut', tailed[:-7]),
            ('a packed size too large', tailed[:1].replace(b'\x10', b'\x90') + tailed[1:end] + struct.pack('<I', 1 << 21) + b'\0' * 64)):
        chk.put('bad/tail.LBA', b)
        st, lines, err = chk.run('-t', 'legacy', 'bad/tail.LBA', 'bad/out.LBA')
        want, drops = Save(b, chk.game).convert('legacy', b[1:b.index(0, 1)])
        if st != 0 or chk.get('bad/out.LBA') != want or not any('dropped, ' in l for l in err):
            chk.fail('%s: status %d, %s' % (what, st, err[:1]))


def contexts(chk, files):
    """Today's context damaged at each of its counts, its CRC-32 made
    again: read as its record, with the line."""
    g = chk.game
    b = files['S4000.LBA']
    name, end = record(b)
    t = bytearray(b[end:-4])
    c = NPIC
    pl = g.places(1)
    patch = c + 160 + ACTOR * 2
    extras = patch + 8 + sum(n for n, off in pl)
    zones = extras + 1 + NEXTRAS * EXTRA
    overlays = zones + 4 + 12 * 2
    grid = overlays + 1 + INCRUST + 12 + OWN
    for what, at, fmt, value in (
            ('no actor', c + 156, '<i', 0), ('101 actors', c + 156, '<i', 101),
            ('a body flag of 2', c + 160 + 114, '<B', 2), ('a keyframe of -3', c + 160 + 119, '<i', -3),
            ('a patch more', patch, '<i', len(pl) + 1), ('51 extras', extras, '<B', 51),
            ('256 zones', zones, '<i', 256), ('11 overlays', overlays, '<B', 11),
            ('a run past the grid', grid, '<I', NCUBE - 1)):
        d = bytearray(t)
        struct.pack_into(fmt, d, at, value)
        d += struct.pack('<I', crc(d))
        for extra in (b'',) if what != 'no actor' else (b'', b'X'):
            f = b[:end] + bytes(d)
            if extra:
                d2 = bytearray(t) + extra
                f = b[:end] + bytes(d2) + struct.pack('<I', crc(d2))
                what = 'a byte past the seed'
            chk.put('bad/ctx.LBA', f)
            want, drops = Save(f, g).convert('legacy', b[1:name])
            st, lines, err = chk.run('-t', 'legacy', 'bad/ctx.LBA', 'bad/out.LBA')
            if 'damaged' not in drops:
                chk.fail('%s: the model reads it' % what)
            elif st != 0 or chk.get('bad/out.LBA') != want or not any('dropped, ' in l for l in err):
                chk.fail('%s: status %d, %s' % (what, st, err[:1]))


def naming(chk, files):
    """The names a version gives a new save, and those it refuses."""
    chk.fresh('name')
    os.makedirs(chk.path('name', 'lba'))
    chk.put('name/lba/s1.lba', rec(b'one'))
    chk.put('src/S12.LBA', files['S12.LBA'], mtime=1600000000)
    chk.put('src/s0013.lba', rec(b'thirteen'), mtime=1600000000)
    chk.put('src/x.dat.LBA', rec(b'xdat'), mtime=1600000000)
    st, lines, err = chk.run('-t', 'lba', 'src/S12.LBA', 'src/s0013.lba', 'src/x.dat.LBA', 'name/lba')
    want = ['name/lba/S12.LBA', 'name/lba/S13.LBA', 'name/lba/S2.LBA']
    if st != 0 or lines != want:
        chk.fail('lba\'s names: %s, the model %s' % (lines, want))
    st, lines, err = chk.run('-t', 'lba', 'src/S12.LBA', 'name/lba')
    if st != 1 or lines or not any('already' in l for l in err):
        chk.fail('a name another save has: status %d, %s' % (st, lines))
    st, lines, err = chk.run('-t', 'lba', '-n', 'Zoe', 'src/S12.LBA', 'name/lba')
    if st != 0 or lines != ['name/lba/S3.LBA']:
        chk.fail('-n Zoe: %s' % lines)
    chk.put('src/S0.LBA', rec(b'Zero'), mtime=1600000000)
    os.makedirs(chk.path('name', 'zero'))
    st, lines, err = chk.run('-t', 'lba', 'src/S0.LBA', 'name/zero')
    if st != 0 or lines != ['name/zero/S0.LBA']:
        chk.fail('S0.LBA takes its own number: %s' % lines)
    os.makedirs(chk.path('name', 'dot'))
    chk.put('src/a.LBA', rec(b'Twinsen', version=VDOTEMU) + struct.pack('<q', 1), mtime=1600000000)
    chk.put('src/b.LBA', rec(b'Twinsen', version=VDOTEMU) + struct.pack('<q', 2), mtime=1600000000)
    chk.put('src/20200509161619.LBA', rec(b'Twinsen', version=VDOTEMU) + struct.pack('<q', 3), mtime=1600000000)
    st, lines, err = chk.run('-t', 'dotemu', 'src/a.LBA', 'src/b.LBA', 'src/20200509161619.LBA', 'name/dot')
    t = time.strftime('%Y%m%d%H%M%S', time.localtime(1600000000))
    t1 = time.strftime('%Y%m%d%H%M%S', time.localtime(1600000001))
    want = ['name/dot/%s.LBA' % t, 'name/dot/%s.LBA' % t1, 'name/dot/20200509161619.LBA']
    if st != 0 or lines != want:
        chk.fail('DotEmu\'s names: %s, the model %s' % (lines, want))
    os.makedirs(chk.path('name', 'legacy'))
    st, lines, err = chk.run('-t', 'legacy', 'syn/current.lba', 'name/legacy')
    if st != 1 or not any('its own save' in l for l in err):
        chk.fail('a player\'s save named CURRENT: status %d' % st)
    st, lines, err = chk.run('-t', 'legacy', '-n', 'cur', 'syn/current.lba', 'name/legacy')
    if st != 0 or lines != ['name/legacy/S1.LBA']:
        chk.fail('current.lba named cur: %s' % lines)
    for version, n, ok in (('lbawin', 28, True), ('lbawin', 29, False), ('lba', 29, True), ('dotemu', 29, False)):
        src = 'syn/name28.LBA' if n == 28 else 'syn/name29.LBA'
        st, lines, err = chk.run('-t', version, src, 'name/n.LBA')
        if (st == 0) != ok:
            chk.fail('a name of %d for %s: status %d' % (n, version, st))


def usage(chk):
    """The usages refused, each with status 2."""
    for args in (
            [], ['-t'], ['-t', 'lbaexe', 'syn/S12.LBA', 'u.LBA'], ['-t', 'lba', 'syn/S12.LBA'],
            ['-t', 'lba', 'syn/S12.LBA', 'u.dat'], ['-t', 'lba', 'syn/S12.LBA', 'syn/S12.LBA', 'u.LBA'],
            ['-t', 'lba', '-n', '', 'syn/S12.LBA', 'u.LBA'], ['-t', 'lba', '-n', 'x' * 30, 'syn/S12.LBA', 'u.LBA'],
            ['-t', 'lba', '-n', 'a', 'syn/S12.LBA', 'syn/S9271.LBA', 'syn'],
            ['-t', 'lba', '-n', 'Fran\xe7ois', 'syn/S12.LBA', 'u.LBA'],
            ['-t', 'lba', '-T', '5', 'syn/S12.LBA', 'u.LBA'], ['-t', 'dotemu', '-T', '356401', 'syn/S12.LBA', 'u.LBA'],
            ['-t', 'dotemu', '-T', '-1', 'syn/S12.LBA', 'u.LBA'], ['-l'], ['-l', '-t', 'lba', 'syn/S12.LBA'],
            ['-x', 'syn/S12.LBA']):
        st, lines, err = chk.run(*args)
        if st != 2:
            chk.fail('usage %s: status %d' % (args, st))


def batch(chk, files):
    """Each version given every save at once, into a directory: the
    saves it writes, in their order, and those it refuses."""
    g = chk.game
    for version, (vb, maxname, marks, wtime, wtail, dated, names) in VERSIONS.items():
        d = 'batch-' + version
        chk.fresh(d)
        taken, oks = set(), []
        for name in sorted(files):
            b = files[name]
            try:
                s = Save(b, g)
                if wtail:
                    s.prepare(g)
            except NoSave:
                continue
            who = b[1:s.name]
            if wtail and who.upper() in (b'AUTOSAVE', b'CURRENT') or len(who) > maxname:
                continue
            if names and who.lower() in taken:
                continue
            taken.add(who.lower())
            oks.append((name, s, who))
        args = ['-t', version] + (['-g', g.dir] if g.dir else [])
        st, lines, err = chk.run(*(args + ['syn/' + n for n in sorted(files)] + [d]))
        if len(lines) != len(oks):
            chk.fail('%s at once: %d written, the model %d' % (version, len(lines), len(oks)))
            continue
        for (name, s, who), line in zip(oks, lines):
            want, drops = s.convert(version, who)
            chk.compare('%s at once to %s' % (name, version), chk.get(line), want,
                        wtail and s.tail is not None)


def listall(chk, files):
    """-l on every save at once: each one's fields."""
    st, lines, err = chk.run('-l', *['syn/' + n for n in sorted(files)])
    got = {}
    for l in lines:
        if l.startswith('file\t'):
            cur = got.setdefault(os.path.basename(l[5:]), {})
        elif '\t' in l:
            k, v = l.split('\t', 1)
            cur[k] = v
    for name in sorted(files):
        b = files[name]
        try:
            s = Save(b, chk.game)
        except NoSave:
            if name in got:
                chk.fail('%s: listed, the model reads no save' % name)
            continue
        f = got.get(name, {})
        want = {'name': b[1:s.name].decode('latin-1'), 'scene': str(s.scene),
                'newgameplus': 'yes' if s.marks else 'no'}
        if s.form is not None:
            want['tail'] = s.form
        for k, v in want.items():
            if f.get(k) != v:
                chk.fail('%s: -l %s %r, the model %r' % (name, k, f.get(k), v))


def windows_checks(chk, files):
    """The checks a Windows exe takes under Wine, a run each costing
    seconds: every version at once, the listing, a file written over,
    the names, a usage, the picture."""
    batch(chk, files)
    listall(chk, files)
    chk.fresh('over')
    chk.put('over/S1.LBA', b'old')
    st, lines, err = chk.run('-t', 'lba', 'syn/S12.LBA', 'over/S1.LBA')
    if st != 0 or chk.get('over/S1.LBA') != Save(files['S12.LBA'], chk.game).convert('lba', b'abc', 'S1.LBA')[0] \
            or os.path.exists(chk.path('over', 'S1.LBA.tmp')):
        chk.fail('a file written over: status %d' % st)
    chk.fresh('name')
    chk.put('name/s1.lba', rec(b'one'))
    st, lines, err = chk.run('-t', 'lba', 'syn/S12.LBA', 'syn/S9271.LBA', 'syn/trail.LBA', 'name')
    if lines != ['name/S12.LBA', 'name/S9271.LBA'] or st != 1:
        chk.fail('names in a directory: %s, status %d' % (lines, st))
    chk.fresh('dot')
    st, lines, err = chk.run('-t', 'dotemu', 'syn/S12.LBA', 'dot')
    want = 'dot/%s.LBA' % time.strftime('%Y%m%d%H%M%S', time.localtime(1600000000))
    if lines != [want]:
        chk.fail('DotEmu\'s name: %s, the model %s' % (lines, want))
    st, lines, err = chk.run('-t', 'lba', 'syn/S12.LBA', 'u.dat')
    if st != 2:
        chk.fail('a usage: status %d' % st)
    if chk.game.dir is not None:
        game_checks(chk, files)


def check(args):
    windows = False
    gamedir = corpus = None
    while args and args[0].startswith('-'):
        if args[0] == '-w':
            windows = True
            args = args[1:]
        elif args[0] == '-g' and len(args) > 1:
            gamedir, args = os.path.abspath(args[1]), args[2:]
        elif args[0] == '-c' and len(args) > 1:
            corpus, args = os.path.abspath(args[1]), args[2:]
        else:
            usage_exit()
    if len(args) != 1:
        usage_exit()
    tool = os.path.abspath(args[0])
    work = tempfile.mkdtemp(prefix='lbasave.')
    if windows:
        shutil.copy(tool, os.path.join(work, 'lbasave.exe'))
        tool = os.path.join(work, 'lbasave.exe')
    chk = Check(tool, windows, Game(gamedir), work)
    try:
        files = synthetic(chk)
        if windows:
            windows_checks(chk, files)
            return finish(chk)
        for name, b in files.items():
            src = 'syn/' + name
            chk.listing(name, src, b)
            try:
                Save(b, chk.game)
            except NoSave:
                continue
            for version in VERSIONS:
                chk.fresh('out')
                out = 'out/' + ('current.lba' if name == 'current.lba' and version == 'legacy' else 'out.LBA')
                if version == 'legacy' and name in ('autosave.lba', 'current.lba') and out == 'out/out.LBA':
                    st, lines, err = chk.run('-t', version, src, out)
                    if st != 1:
                        chk.fail('%s to a player\'s save of LBA1 Legacy: status %d' % (name, st))
                    continue
                n = len(b[1:b.index(0, 1)])
                if n > VERSIONS[version][1]:
                    continue
                chk.convert(name, src, b, version, out=out)
        twice = []
        for k in range(2):
            chk.fresh('twice%d' % k)
            st, lines, err = chk.run('-t', 'legacy', 'syn/S12.LBA', 'syn/S2075.LBA', 'syn/S5534.LBA', 'twice%d' % k)
            twice.append([chk.get(l) for l in lines] if st == 0 else None)
        if twice[0] is None or twice[0] != twice[1]:
            chk.fail('two runs give other bytes')
        for k, name in enumerate(('S12.LBA', 'S2075.LBA', 'S5534.LBA')):
            chk.fresh('one')
            st, lines, err = chk.run('-t', 'legacy', 'syn/' + name, 'one')
            if st != 0 or twice[0] is None or chk.get(lines[0]) != twice[0][k]:
                chk.fail('%s alone gives other bytes than among others' % name)
        chk.convert('S2075.LBA -n', 'syn/S2075.LBA', files['S2075.LBA'], 'legacy', name=b'Zed', out='out/S9.LBA')
        chk.convert('S12.LBA -T', 'syn/S12.LBA', files['S12.LBA'], 'dotemu', time=3600, out='out/T.LBA')
        batch(chk, files)
        damaged(chk, files['S12.LBA'], files['S4000.LBA'])
        contexts(chk, files)
        naming(chk, files)
        usage(chk)
        if gamedir is not None:
            game_checks(chk, files)
        if corpus is not None:
            corpus_checks(chk, corpus)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return finish(chk)


def finish(chk):
    print('saves.py: %d runs of lbasave, %d differences' % (chk.runs, chk.fails))
    return 1 if chk.fails else 0


def game_checks(chk, files):
    """DotEmu's picture, from the palette of the game's RESS.HQR."""
    pal = chk.game.entry('RESS.HQR', 0)
    tail = Save(files['current.lba'], chk.game).tail
    chk.fresh('pic')
    st, lines, err = chk.run('-t', 'dotemu', '-g', chk.game.dir, 'syn/current.lba', 'pic/P.LBA')
    rows = png(chk.path('pic', 'P.LBA.png')) if st == 0 else None
    if rows is None or rows != picture(tail, pal):
        chk.fail('DotEmu\'s picture differs from the model\'s')


def corpus_checks(chk, corpus):
    """Each save of corpus listed and converted to every version."""
    for root, dirs, names in os.walk(corpus):
        dirs.sort()
        for name in sorted(names):
            if not name.lower().endswith('.lba'):
                continue
            path = os.path.join(root, name)
            rel = os.path.relpath(path, chk.work)
            b = open(path, 'rb').read()
            chk.listing(name, rel, b)
            try:
                s = Save(b, chk.game)
            except NoSave:
                continue
            for version in VERSIONS:
                n = s.name - 1
                if n > VERSIONS[version][1]:
                    continue
                out = 'out/out.LBA'
                who = b[1:s.name]
                if version == 'legacy' and who.upper() in (b'AUTOSAVE', b'CURRENT'):
                    out = 'out/%s.lba' % ('autosave' if who.upper() == b'AUTOSAVE' else 'current')
                if version == 'legacy' and s.snap is not None and s.why is None:
                    try:
                        s.prepare(chk.game)
                    except NoSave:
                        st, lines, err = chk.run('-t', 'legacy', '-g', chk.game.dir or '.', rel, out)
                        if st != 1:
                            chk.fail('%s: an upgrade the model refuses: status %d' % (name, st))
                        continue
                chk.fresh('out')
                chk.convert(name, rel, b, version, out=out)
                if version == 'legacy' and b[0] == VPORT | PACKED and s.form == '16' and s.why is None \
                        and chk.get(out) != b:
                    chk.fail('%s: packed again to other bytes than LBA1 Legacy\'s' % name)


def usage_exit():
    sys.stderr.write('usage: saves.py check [-w] [-g gamedir] [-c corpus] lbasave\n')
    sys.exit(2)


def main():
    if len(sys.argv) < 2 or sys.argv[1] != 'check':
        usage_exit()
    sys.exit(check(sys.argv[2:]))


if __name__ == '__main__':
    main()
