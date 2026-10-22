"""psmusic.py: the PlayStation's track of each tune, as its executable
asks for it, and the file that holds each track on the port's CD, as
its disc names it, proved and written as sources/psmusic.c.

    psmusic.py [-x exe] [-i cue] [-c file]

exe is the PlayStation's SLES_006.98, $LBATMP/ps's by default (the
tree's ../lba1-tmp when LBATMP is unset, tools/extract ps puts it
there), and cue the cue sheet of its Redump image in $LBAISO (../iso by
default). psmusic.py writes the C on its standard output, or with -c
compares it with file and fails if they differ. It exits 1 when a proof
fails, saying which.

PlayMusic (0x80046674) plays a tune from the disc. When the drive is
busy (0x800628d0) it stops it (0x80062a30) and waits (0x80062b30). A
tune of -1, or the CD music switch off (the halfword at 0x8009de82),
returns, and any other tune's track is asked for (0x80062810).
psmusic.py runs its code on every tune from -1 to 255, each passed as
its callers pass a tune, a halfword sign-extended, in an interpreter
of the R3000 instructions the function holds, each branch's and jump's
delay slot run before its target. Its calls are answered: 0x800628d0
says the drive is idle, so that nothing is stopped, and 0x80062810
records the track. The switch reads the image's own value, which must
be on. Any other instruction, call or address, or a run of over 1000
instructions, fails.

The disc's ISO 9660 directory names the files of its audio tracks, each
entry's sector its track's INDEX 01 as the cue sheet places it:
TRACK1.WAV to TRACK9.WAV, and the jingles JINGLES/J01MID.WAV to
J18MID.WAV, J13 absent. PlayMusic must give tune n the track of
TRACKn.WAV for n from 1 to 9, and a track the directory names to every
tune it plays. On the port's CD tunes 1 to 9 play from the PC CD's
tracks 2 to 10, TRACK02.WAV to TRACK10.WAV, which hold the same
recordings, and a jingle from its name on the disc.

The C holds TabPsTrack, the track of tunes 0 to 30, 0 for none,
PlayMusic's whole, TabPsFile, the port's file of each track, and
GetPsTrackFile, the file of a tune. Tune 29 takes the track of
J18MID.WAV, the recording of tune 30, as on the PlayStation.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.environ.get('LBATMP') or os.path.join(HERE, '..', '..', 'lba1-tmp')
ISO = os.environ.get('LBAISO') or os.path.join(HERE, '..', '..', 'iso')
CUE = 'Little Big Adventure (Europe) (En,Fr,De,Es,It).cue'

PLAYMUSIC = 0x80046674
IDLE = 0x800628d0
ASK = 0x80062810
SWITCH = 0x8009de82
STACK = 0x801ff000
RETURN = 0xfffffff0
STEPS = 1000

NTUNES = 31
TUNE29 = 29
MASK = 0xffffffff

RAW = 2352
COOKED = 2048


class Fail(Exception):
    pass


def u32(b, p):
    return struct.unpack_from('<I', b, p)[0]


def signed(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


class Exe:
    """A PS-X EXE: its text, loaded at its address."""

    def __init__(self, path):
        with open(path, 'rb') as f:
            d = f.read()
        if d[:8] != b'PS-X EXE':
            raise Fail('%s: no PS-X EXE' % path)
        self.base = u32(d, 0x18)
        self.text = d[0x800:0x800 + u32(d, 0x1c)]

    def inside(self, addr, n):
        return self.base <= addr and addr + n <= self.base + len(self.text)


def run(exe, tune):
    """PlayMusic run on tune: the tracks it asks for, in order."""
    r = [0] * 32
    stack = {}
    asked = []

    def load(addr, n):
        if addr % n != 0:
            raise Fail('tune %d: a load at 0x%08x unaligned' % (tune, addr))
        if exe.inside(addr, n):
            return int.from_bytes(exe.text[addr - exe.base:addr - exe.base + n], 'little')
        if addr in stack and n == 4:
            return stack[addr]
        raise Fail('tune %d: a load of %d bytes from 0x%08x' % (tune, n, addr))

    def store(addr, v):
        if not STACK - 0x100 <= addr < STACK or addr % 4 != 0:
            raise Fail('tune %d: a store to 0x%08x' % (tune, addr))
        stack[addr] = v

    def step(pc):
        """Run the instruction at pc: its target when it branches or
        jumps (pc + 8 when not taken), else None."""
        w = load(pc, 4)
        op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        imm = w & 0xffff
        simm = signed(imm, 16)
        target = None
        if op == 0:
            fn, sh = w & 63, (w >> 6) & 31
            if fn == 0x00:
                v = (r[rt] << sh) & MASK
            elif fn == 0x03:
                v = (signed(r[rt], 32) >> sh) & MASK
            elif fn == 0x21:
                v = (r[rs] + r[rt]) & MASK
            elif fn == 0x08:
                return r[rs]
            else:
                raise Fail('tune %d: at 0x%08x, function 0x%02x' % (tune, pc, fn))
            r[rd] = v
        elif op in (0x02, 0x03):
            target = ((pc + 4) & 0xf0000000) | (w & 0x3ffffff) << 2
            if op == 0x03:
                r[31] = pc + 8
            return target
        elif op in (0x04, 0x05):
            taken = (r[rs] == r[rt]) == (op == 0x04)
            return (pc + 4 + (simm << 2)) & MASK if taken else pc + 8
        elif op == 0x09:
            r[rt] = (r[rs] + simm) & MASK
        elif op == 0x0b:
            r[rt] = int(r[rs] < simm & MASK)
        elif op == 0x0c:
            r[rt] = r[rs] & imm
        elif op == 0x0d:
            r[rt] = r[rs] | imm
        elif op == 0x0f:
            r[rt] = imm << 16
        elif op == 0x21:
            r[rt] = signed(load((r[rs] + simm) & MASK, 2), 16) & MASK
        elif op == 0x23:
            r[rt] = load((r[rs] + simm) & MASK, 4)
        elif op == 0x2b:
            store((r[rs] + simm) & MASK, r[rt])
        else:
            raise Fail('tune %d: at 0x%08x, opcode 0x%02x' % (tune, pc, op))
        r[0] = 0
        return target

    r[4] = signed(tune, 16) & MASK
    r[29] = STACK
    r[31] = RETURN
    pc = PLAYMUSIC
    for _ in range(STEPS):
        if pc == RETURN:
            return asked
        call = load(pc, 4) >> 26 == 0x03
        target = step(pc)
        r[0] = 0
        if target is None:
            pc += 4
            continue
        if step(pc + 4) is not None:
            raise Fail('tune %d: at 0x%08x, a jump in a delay slot' % (tune, pc + 4))
        r[0] = 0
        if not call:
            pc = target
        elif target == IDLE:
            r[2] = MASK
            pc = r[31]
        elif target == ASK:
            asked.append(signed(r[4], 32))
            pc = r[31]
        else:
            raise Fail('tune %d: at 0x%08x, a call of 0x%08x' % (tune, pc, target))
    raise Fail('tune %d: over %d instructions' % (tune, STEPS))


def msf(s):
    m, sec, f = (int(x) for x in s.split(':'))
    return (m * 60 + sec) * 75 + f


def tracks(cue):
    """The disc's audio tracks: each one's number by its INDEX 01's
    sector, and the data track's file."""
    base = os.path.dirname(cue)
    out = {}
    data = None
    start = 0
    size = 0
    number = None
    for line in open(cue, encoding='latin-1'):
        w = line.strip()
        m = re.match(r'FILE\s+"(.*)"\s+BINARY$', w)
        if m:
            start += size
            path = os.path.join(base, m.group(1))
            size = os.path.getsize(path) // RAW
            if data is None:
                data = path
            continue
        m = re.match(r'TRACK\s+(\d+)\s+(\S+)$', w)
        if m:
            number = int(m.group(1)) if m.group(2) == 'AUDIO' else None
            continue
        m = re.match(r'INDEX\s+01\s+(\d+:\d+:\d+)$', w)
        if m and number is not None:
            out[start + msf(m.group(1))] = number
    if data is None:
        raise Fail('%s: no FILE' % cue)
    return out, data


def directory(path):
    """The files of the ISO 9660 volume in a raw data track, mode 2 form 1:
    each one's path, sector and size."""
    with open(path, 'rb') as f:
        def read(lba, n):
            b = bytearray()
            for i in range((n + COOKED - 1) // COOKED):
                f.seek((lba + i) * RAW + 24)
                b += f.read(COOKED)
            return bytes(b[:n])

        if read(16, COOKED)[1:6] != b'CD001':
            raise Fail('%s: no ISO 9660 volume in mode 2 form 1' % path)
        out = []

        def walk(lba, size, prefix):
            d = read(lba, size)
            o = 0
            while o < len(d):
                n = d[o]
                if n == 0:
                    o = (o // COOKED + 1) * COOKED
                    continue
                elba, esize = struct.unpack_from('<I4xI', d, o + 2)
                flags = d[o + 25]
                name = d[o + 33:o + 33 + d[o + 32]]
                o += n
                if name in (b'\0', b'\1'):
                    continue
                name = name.decode('latin-1').split(';')[0]
                if flags & 2:
                    walk(elba, esize, prefix + name + '/')
                else:
                    out.append((prefix + name, elba, esize))

        walk(*struct.unpack_from('<I4xI', read(16, COOKED), 158), '/')
        return out


def table(exepath, cue):
    """The track of each tune, as PlayMusic asks for it, and the port's
    file of each track, proved."""
    exe = Exe(exepath)
    if not exe.inside(SWITCH, 2) or exe.text[SWITCH - exe.base:SWITCH - exe.base + 2] == b'\0\0':
        raise Fail('the CD music switch at 0x%08x is off in the image' % SWITCH)
    track = {}
    for tune in range(-1, 256):
        asked = run(exe, tune)
        if len(asked) > 1:
            raise Fail('tune %d: tracks %s asked for' % (tune, asked))
        if asked:
            track[tune] = asked[0]
    if any(t < 0 or t >= NTUNES for t in track):
        raise Fail('tunes %s past the table play' % sorted(t for t in track if t < 0 or t >= NTUNES))
    starts, data = tracks(cue)
    named = {}
    for path, lba, size in directory(data):
        if lba in starts:
            named[starts[lba]] = path
    for n in range(1, 10):
        if named.get(track.get(n)) != '/TRACK%d.WAV' % n:
            raise Fail('tune %d: track %s, not TRACK%d.WAV' % (n, track.get(n), n))
    files = {}
    for tune, t in sorted(track.items()):
        if t not in named:
            raise Fail('tune %d: track %d is in no directory entry' % (tune, t))
        m = re.match(r'/TRACK(\d)\.WAV$', named[t])
        files[t] = 'TRACK%02d.WAV' % (int(m.group(1)) + 1) if m else named[t][1:].replace('/', '\\')
    return track, files, named


def wrap(items, lines):
    line = '\t'
    for item in items:
        if len(line.expandtabs()) + len(item) > 72:
            lines.append(line.rstrip())
            line = '\t'
        line += item + ' '
    lines.append(line.rstrip())


def comment(paragraphs):
    """A block comment holding paragraphs, its lines filled to 72
    columns."""
    lines = ['/*']
    for i, text in enumerate(paragraphs):
        if i > 0:
            lines.append(' *')
        line = ' *'
        for word in text.split():
            if len(line) + 1 + len(word) > 72:
                lines.append(line)
                line = ' *'
            line += ' ' + word
        lines.append(line)
    return lines + [' */']


def c(track, files, named):
    """sources/psmusic.c's text."""
    j18 = named[track[TUNE29]].split('/')[-1]
    lines = ['#include <stddef.h>', '#include <stdint.h>', '#include "../sys/compat.h"',
             '#include "c_extern.h"', '',
             '/* Made by tools/psmusic.py, which proves them. Do not edit. */', '']
    lines += comment([
        "FEATURE: the PlayStation's recordings of the tunes, which it plays "
        "from its disc. TabPsTrack is the track of tunes 0 to %d as its "
        "PlayMusic (SLES_006.98 0x%08x) asks for it, 0 for none: tunes 1 to "
        "8 tracks 2 to 9, tune 9 track %d, the jingles tracks %d to %d. "
        "Taken from the PlayStation."
        % (NTUNES - 1, PLAYMUSIC, track[9], min(t for t in track.values() if t > 9 and t != track[9]),
           max(t for t in track.values() if t != track[9])),
        "Tune %d, a sequence of 39.1 s, takes track %d, %s, the "
        "recording of tune 30, whose music lasts 27.0 s as tune 30's "
        "sequence of 26.2 s, as the PlayStation plays it."
        % (TUNE29, track[TUNE29], j18)])
    lines.append('static uint8_t TabPsTrack[%d] = {' % NTUNES)
    wrap(['%d,' % track.get(tune, 0) for tune in range(NTUNES)], lines)
    lines += ['};', '']
    lines += comment([
        "TabPsFile is the file of each track on the port's CD: the "
        "disc's TRACK1.WAV to TRACK9.WAV, tunes 1 to 9, as the PC CD's "
        "tracks 2 to 10, which hold the same recordings, and its jingles "
        "by their names."])
    last = max(files)
    lines.append('static char *TabPsFile[%d] = {' % (last + 1))
    for t in sorted(files):
        lines.append('\t[%d] = "%s",' % (t, files[t].replace('\\', '\\\\')))
    lines += ['};', '',
              "/* GetPsTrackFile: the file on the CD of tune num's track, or NULL */",
              'char*',
              'GetPsTrackFile(int32_t num)',
              '{',
              '\tif (num < 0 || num >= %d)' % NTUNES,
              '\t\treturn NULL;',
              '\treturn TabPsFile[TabPsTrack[num]];',
              '}']
    return '\n'.join(lines) + '\n'


def usage():
    print('usage: psmusic.py [-x exe] [-i cue] [-c file]', file=sys.stderr)
    sys.exit(2)


def main(args):
    exe = os.path.join(TMP, 'ps', 'SLES_006.98')
    cue = os.path.join(ISO, CUE)
    check = None
    while args:
        if len(args) < 2 or args[0] not in ('-x', '-i', '-c'):
            usage()
        f, v = args.pop(0), args.pop(0)
        if f == '-x':
            exe = v
        elif f == '-i':
            cue = v
        else:
            check = v
    try:
        text = c(*table(exe, cue))
    except Fail as e:
        print('psmusic.py: %s' % e, file=sys.stderr)
        sys.exit(1)
    if check is None:
        sys.stdout.write(text)
        return
    with open(check) as f:
        if f.read() != text:
            print('psmusic.py: %s differs' % check, file=sys.stderr)
            sys.exit(1)


main(sys.argv[1:])
