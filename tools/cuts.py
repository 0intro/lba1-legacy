"""cuts.py: the tunes DotEmu plays as recordings, each a passage it cut
in 2014 from a track of the CD, measured against the PlayStation's
tracks, proved, and written as sys/cuts.c.

    cuts.py [-x pcm] [-d dir] [-t cd] [-c file]

dir holds DotEmu's recordings, LBA1-01.ogg to LBA1-33.ogg, tune n's
LBA1-(n+1) (Steam's DotEmu/resources/LBA_FILES/MIDI_MP3/OGG in $LBATMP
by default, the tree's ../lba1-tmp when LBATMP is unset), cd the CD
whose directory holds the PlayStation's tracks as track02.wav to
track10.wav ($LBATMP/pcmusic, which tools/cdtracks.py writes from its
disc's image), and pcm the tool that decodes a recording as the port
does (build/linux-amd64-gcc/pcm). cuts.py writes the C on its standard
output, or with -c proves the cuts file holds against the recordings:
each tune's track, its first sample within one of the place measured and
its samples within two, its mark the track's, its gains within 0.04 of
the fade measured, and the proof below, so that a decoder that rounds
its last bits otherwise, as a 32-bit build's does, passes. It exits 1
when a measure or a proof fails, saying which. A sample here is a pair,
left and right, of 16-bit values at 44100 Hz.

The recordings of tunes 1, 3 to 9 and 32 hold whole tracks, tune n's track
n+1, tunes 8, 9 and 32 track 10, as the XMIDI's entry 8 holds tune 9's
sequence: their cuts are the tracks whole. Tunes 26 and 31 keep their
XMIDI: LBA1-27 is a rendering of its sequence, LBA1-32 another
recording. Each other recording, the jingles' and tune 2's, is a passage
of the track its table names, from about the second it gives:

- Its place. A window of 4096 samples a second into the recording is
  sought within 1024 samples of the second the table gives, and windows
  every 2 s are then sought within 6 samples of the place the line
  through those found before gives, each found where the correlation of
  the waveforms peaks at 0.98 or more, between samples by a parabola
  through the peak. A line through them, those off it by more than half
  a sample dropped, gives the sample of the track that the recording's
  first sample holds and the track's samples for each of its own, which
  must lie within 100 ppm of 1.

- Its gain. In windows of 2048 samples, the recording's samples
  projected on the track's at their place, where the track's samples
  are louder than 300 in root mean square, give the window's gain, the
  median of those before the last 40% its steady gain, by which the
  others are divided, and each the median of its 5 neighbours. The
  recording starts at its steady gain, at 0.97 or more. Its fade is the
  windows from the last at 0.97 or more to the last above 0.02, drawn
  as the fewest knots, by Douglas and Peucker's division, that keep the
  gains within 0.02, each a sample from the cut's first and a gain in
  4096ths, at most 4096, the last at 0.

The cut is the track's samples from the recording's first, or from the
track's start when the recording starts before it, as many as the
recording holds. Its mark is the CRC-32 of the 32 samples a second into
it, their bytes as the track holds them, a mark unique within 4096
samples.

Each cut is then proved: its samples scaled by its knots as sys/cd.c
scales them, each value times the gain at its sample over 4096 in
integers, must follow the recording, in segments of a second where the
recording is loud, each set against the cut where the line puts its
middle, at a median correlation of 0.98 or more, and each segment
before the fade at its steady gain within 0.04, as the transfer's own
level wanders.
"""
import math
import os
import re
import struct
import subprocess
import sys
import wave
import zlib
from array import array

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.environ.get('LBATMP') or os.path.join(HERE, '..', '..', 'lba1-tmp')

NTUNE = 33
RATE = 44100
SECOND = RATE
WHOLE = {1: 2, 3: 4, 4: 5, 5: 6, 6: 7, 7: 8, 8: 10, 9: 10, 32: 10}
# The tunes cut from a track: the track and about the second its
# passage starts from.
PASSAGE = {
    0: (8, 0.00), 2: (3, 0.00), 10: (2, 138.19), 11: (7, 29.99), 12: (6, 50.10),
    13: (6, 74.03), 14: (4, 127.86), 15: (4, 38.55), 16: (2, 65.44), 17: (2, 0.00),
    18: (8, 0.00), 19: (6, 86.49), 20: (8, 63.68), 21: (8, 93.72), 22: (8, 28.09),
    23: (2, 41.81), 24: (10, 44.44), 25: (10, 22.21), 27: (7, 65.01), 28: (8, 50.58),
    29: (3, 58.54), 30: (3, 103.26),
}
SEEK = 1024          # samples, about the table's second
NEAR = 6             # samples, about the line
WIN = 4096           # samples, a window sought
STEP = 2 * SECOND    # samples between the windows sought
PEAK = 0.98
GAINWIN = 2048
LOUD = 300
STEADY = 0.97
SILENT = 0.02
TOLERANCE = 0.02
LEVEL = 0.04
UNIT = 4096
MARK = 32
MARKAT = SECOND
MARKSEEK = 4096
SEGMENT = SECOND
NKNOT = 16


class Fail(Exception):
    pass


def recording(pcm, path):
    """A recording's samples, interleaved, as pcm decodes them."""
    info = subprocess.run([pcm, '-i', path], capture_output=True, text=True)
    if info.returncode != 0:
        raise Fail('%s: %s' % (path, info.stderr.strip()))
    rate, channels, frames = (int(x) for x in info.stdout.split())
    if rate != RATE or channels != 2:
        raise Fail('%s: %d Hz, %d channels, where 44100 Hz and 2 were due' % (path, rate, channels))
    out = subprocess.run([pcm, path], capture_output=True)
    if out.returncode != 0:
        raise Fail('%s: %s' % (path, out.stderr.decode().strip()))
    a = array('h', out.stdout)
    if sys.byteorder == 'big':
        a.byteswap()
    if len(a) != 2 * frames:
        raise Fail('%s: %d samples, where %d were due' % (path, len(a), 2 * frames))
    return a


def track(cd, n):
    """Track n's samples, interleaved."""
    path = os.path.join(cd, 'track%02d.wav' % n)
    with wave.open(path, 'rb') as w:
        if w.getframerate() != RATE or w.getnchannels() != 2 or w.getsampwidth() != 2:
            raise Fail('%s: not 16-bit stereo at 44100 Hz' % path)
        a = array('h', w.readframes(w.getnframes()))
    if sys.byteorder == 'big':
        a.byteswap()
    return a


def seek(f, i, c, lo, hi, n):
    """The sample of c from lo to hi where n samples of f from sample i
    correlate best, between samples by a parabola through the peak, and
    the peak."""
    lo = max(lo, 0)
    hi = min(hi, len(c) // 2 - n)
    if hi - lo < 2:
        return None, 0.0
    a = f[2 * i:2 * (i + n)]
    ea = math.sumprod(a, a)
    r = []
    for j in range(lo, hi + 1):
        b = c[2 * j:2 * (j + n)]
        e = ea * math.sumprod(b, b)
        r.append(math.sumprod(a, b) / math.sqrt(e) if e > 0 else 0.0)
    k = max(range(len(r)), key=r.__getitem__)
    d = 0.0
    if 0 < k < len(r) - 1:
        den = r[k - 1] - 2 * r[k] + r[k + 1]
        if den < 0:
            d = 0.5 * (r[k - 1] - r[k + 1]) / den
    return lo + k + d, r[k]


def line(points):
    """The least squares line through points: its value at 0 and its
    slope."""
    n = len(points)
    mx = sum(p for p, _ in points) / n
    my = sum(q for _, q in points) / n
    sxx = sum((p - mx) ** 2 for p, _ in points)
    sxy = sum((p - mx) * (q - my) for p, q in points)
    slope = sxy / sxx if sxx > 0 else 1.0
    return my - slope * mx, slope


def place(f, c, at, name):
    """The sample of c the recording f's first sample holds, and c's
    samples for each of f's."""
    frames = len(f) // 2
    i = min(SECOND, frames - WIN)
    j, peak = seek(f, i, c, int(at * RATE) + i - SEEK, int(at * RATE) + i + SEEK, WIN)
    if j is None or peak < PEAK:
        raise Fail('%s: no place near %.2f s (peak %.3f)' % (name, at, peak))
    points = [(i, j)]
    first, slope = j - i, 1.0
    for i in range(i + STEP, frames - WIN, STEP):
        q = first + slope * i
        j, peak = seek(f, i, c, int(round(q)) - NEAR, int(round(q)) + NEAR, WIN)
        if j is None or peak < PEAK:
            continue
        points.append((i, j))
        if len(points) > 1:
            first, slope = line(points)
    for _ in range(3):
        if len(points) < 2:
            break
        first, slope = line(points)
        kept = [(p, q) for p, q in points if abs(first + slope * p - q) <= 0.5]
        if len(kept) == len(points):
            break
        points = kept
    if len(points) < 3:
        raise Fail('%s: %d windows placed' % (name, len(points)))
    if abs(slope - 1) > 100e-6:
        raise Fail('%s: %.1f ppm' % (name, (slope - 1) * 1e6))
    return first, slope


def median(v):
    v = sorted(v)
    n = len(v)
    if n == 0:
        return None
    return v[n // 2] if n % 2 else 0.5 * (v[n // 2 - 1] + v[n // 2])


def gains(f, c, first, slope):
    """Each window's gain, None where the track is quiet or absent."""
    out = []
    for k in range(len(f) // 2 // GAINWIN):
        p = k * GAINWIN
        q = int(round(first + slope * p))
        if q < 0 or q + GAINWIN > len(c) // 2:
            out.append(None)
            continue
        a = f[2 * p:2 * (p + GAINWIN)]
        b = c[2 * q:2 * (q + GAINWIN)]
        e = math.sumprod(b, b)
        if e < LOUD * LOUD * 2 * GAINWIN:
            out.append(None)
            continue
        out.append(math.sumprod(a, b) / e)
    return out


def smooth(g):
    out = []
    for k in range(len(g)):
        v = [x for x in g[max(0, k - 2):k + 3] if x is not None]
        out.append(median(v))
    return out


def fill(g):
    """g with each None drawn on the line between its neighbours."""
    out = list(g)
    known = [k for k, x in enumerate(out) if x is not None]
    if not known:
        return [0.0] * len(out)
    for k in range(len(out)):
        if out[k] is not None:
            continue
        lo = max((i for i in known if i < k), default=None)
        hi = min((i for i in known if i > k), default=None)
        if lo is None:
            out[k] = out[hi]
        elif hi is None:
            out[k] = out[lo]
        else:
            out[k] = out[lo] + (out[hi] - out[lo]) * (k - lo) / (hi - lo)
    return out


def simplify(xs, ys, tol):
    """The indices Douglas and Peucker keep of the polyline xs, ys."""
    keep = {0, len(xs) - 1}
    stack = [(0, len(xs) - 1)]
    while stack:
        a, b = stack.pop()
        if b <= a + 1:
            continue
        worst, at = 0.0, None
        for m in range(a + 1, b):
            y = ys[a] + (ys[b] - ys[a]) * (xs[m] - xs[a]) / (xs[b] - xs[a])
            d = abs(ys[m] - y)
            if d > worst:
                worst, at = d, m
        if worst > tol:
            keep.add(at)
            stack += [(a, at), (at, b)]
    return sorted(keep)


def envelope(g, name):
    """The steady gain, the fade's knots, each a window's sample and a
    gain over the steady one, and the fade's curve they draw, its
    windows' samples and gains."""
    n = len(g)
    steady = median([x for x in g[:int(n * 0.6)] if x is not None])
    if steady is None or steady <= 0:
        raise Fail('%s: no steady gain' % name)
    s = smooth([None if x is None else x / steady for x in g])
    start = next((x for x in s if x is not None), None)
    if start is None or start < STEADY:
        raise Fail('%s: starts at %s of its gain' % (name, start))
    end = max((k for k, x in enumerate(s) if x is not None and x > SILENT), default=None)
    if end is None:
        raise Fail('%s: silent' % name)
    begin = max(k for k, x in enumerate(s[:end + 1]) if x is not None and x >= STEADY)
    curve = fill(s[begin:end + 1]) + [0.0]
    xs = [(begin + k) * GAINWIN + GAINWIN // 2 for k in range(len(curve))]
    keep = simplify(xs, curve, TOLERANCE)
    knots = [(xs[k], min(1.0, max(0.0, curve[k]))) for k in keep]
    knots[0] = (knots[0][0], 1.0)
    knots[-1] = (knots[-1][0], 0.0)
    return steady, knots, (xs, curve)


def mark(c, at):
    return zlib.crc32(c[2 * at:2 * (at + MARK)].tobytes() if sys.byteorder == 'little'
                      else bytes(struct.pack('<%dh' % (2 * MARK), *c[2 * at:2 * (at + MARK)])))


def cdiv(p, q):
    """p over q truncated toward 0, as C divides."""
    t = abs(p) // abs(q)
    return t if (p >= 0) == (q > 0) else -t


def gainat(knots, i):
    """The gain at sample i of the cut, in 4096ths, as sys/cd.c draws it."""
    if not knots or i < knots[0][0]:
        return UNIT
    for (a, ga), (b, gb) in zip(knots, knots[1:]):
        if i < b:
            return ga + cdiv((gb - ga) * (i - a), b - a)
    return knots[-1][1]


def scaled(s, g):
    """A sample times g over 4096, as sys/cd.c scales it."""
    return cdiv(s * g, UNIT)


def prove(f, c, cut, first, slope, steady, name):
    """Fail unless the cut, scaled as the port scales it, follows the
    recording."""
    t, at, n, knots = cut
    fade = knots[0][0] if knots else n
    peaks = []
    for p in range(0, len(f) // 2 - SEGMENT, SEGMENT):
        a = f[2 * p:2 * (p + SEGMENT)]
        if math.sumprod(a, a) < LOUD * LOUD * 2 * SEGMENT:
            continue
        q = int(round(first + slope * (p + SEGMENT / 2) - SEGMENT / 2)) - at
        if q < 0 or q + SEGMENT > n:
            continue
        if q + SEGMENT <= fade:
            b = c[2 * (at + q):2 * (at + q + SEGMENT)]
        else:
            b = array('h', (scaled(c[2 * (at + q + k) + ch], gainat(knots, q + k))
                            for k in range(SEGMENT) for ch in (0, 1)))
        eb = math.sumprod(b, b)
        if eb == 0:
            continue
        ab = math.sumprod(a, b)
        peaks.append(ab / math.sqrt(math.sumprod(a, a) * eb))
        g = ab / eb / steady
        if q + SEGMENT <= fade and abs(g - 1) > LEVEL:
            raise Fail('%s: the segment at %.1f s has a gain of %.3f' % (name, p / RATE, g))
    m = median(peaks)
    if m is None or m < PEAK:
        raise Fail('%s: the cut follows the recording at %s' % (name, m))
    return m


def marked(c, at, name):
    """The mark of the samples of c from at, which must be found nowhere
    else near."""
    m = mark(c, at)
    for d in range(-MARKSEEK, MARKSEEK + 1):
        if d and 0 <= at + d and mark(c, at + d) == m:
            raise Fail('%s: its mark again %+d samples away' % (name, d))
    return m


def measure(pcm, dir, cd, tune, tracks):
    """Tune's recording measured: its cut, (track, first sample, samples,
    knots, mark), and what proves one, its samples and its track's, its
    place, its rate, its steady gain and the fade's curve on the cut's
    samples."""
    t, at = PASSAGE[tune]
    name = 'LBA1-%02d' % (tune + 1)
    if t not in tracks:
        tracks[t] = track(cd, t)
    c = tracks[t]
    f = recording(pcm, os.path.join(dir, name + '.ogg'))
    first, slope = place(f, c, at, name)
    steady, knots, (xs, curve) = envelope(gains(f, c, first, slope), name)
    start = max(0, int(round(first)))
    n = int(round(first + slope * (len(f) // 2))) - start
    n = min(n, len(c) // 2 - start)

    def on(x):
        return max(0, int(round(first + slope * x)) - start)

    k = [(on(x), int(round(g * UNIT))) for x, g in knots]
    if len(k) > NKNOT:
        raise Fail('%s: %d knots' % (name, len(k)))
    m = marked(c, start + MARKAT, name)
    return (t, start, n, k, m), (f, c, first, slope, steady, [(on(x), g) for x, g in zip(xs, curve)])


def made(pcm, dir, cd):
    """Each tune's cut measured and proved, None for none."""
    tracks = {}
    cuts = {}
    for tune in range(NTUNE):
        if tune in WHOLE:
            cuts[tune] = (WHOLE[tune], 0, 0, [], 0)
        elif tune in PASSAGE:
            cut, (f, c, first, slope, steady, _) = measure(pcm, dir, cd, tune, tracks)
            prove(f, c, cut[:4], first, slope, steady, 'LBA1-%02d' % (tune + 1))
            cuts[tune] = cut
        else:
            cuts[tune] = None
    return cuts


def parse(path):
    """The cuts sys/cuts.c holds, as made gives them."""
    with open(path) as f:
        text = f.read()
    cuts = {tune: None for tune in range(NTUNE)}
    whole = re.compile(r'\t\[(\d+)\] = \{\.track = (\d+)\},')
    for e in whole.finditer(text):
        cuts[int(e.group(1))] = (int(e.group(2)), 0, 0, [], 0)
    entry = re.compile(r'\t\[(\d+)\] = \{(\d+), (\d+), (\d+), (0x[0-9a-f]{8})u, (\d+), \{(.*?)\}\},', re.S)
    for e in entry.finditer(text):
        knots = [(int(a), int(g)) for a, g in re.findall(r'\{(\d+), (\d+)\}', e.group(7))]
        if len(knots) != int(e.group(6)):
            raise Fail('%s: tune %s: %d knots where %s were due' % (path, e.group(1), len(knots), e.group(6)))
        cuts[int(e.group(1))] = (int(e.group(2)), int(e.group(3)), int(e.group(4)), knots, int(e.group(5), 16))
    return cuts


def check(pcm, dir, cd, path):
    """Fail unless the cuts of path are those the recordings give."""
    tracks = {}
    have = parse(path)
    for tune in range(NTUNE):
        cut = have[tune]
        name = 'LBA1-%02d' % (tune + 1)
        if tune in WHOLE:
            if cut != (WHOLE[tune], 0, 0, [], 0):
                raise Fail('%s: tune %d is not track %d whole' % (path, tune, WHOLE[tune]))
            continue
        if tune not in PASSAGE:
            if cut is not None:
                raise Fail('%s: tune %d has a cut' % (path, tune))
            continue
        if cut is None:
            raise Fail('%s: tune %d has no cut' % (path, tune))
        want, (f, c, first, slope, steady, curve) = measure(pcm, dir, cd, tune, tracks)
        t, start, n, knots, m = cut
        if t != want[0] or abs(start - want[1]) > 1 or abs(n - want[2]) > 2:
            raise Fail('%s: track %d from %d for %d where %d from %d for %d were due'
                       % (name, t, start, n, want[0], want[1], want[2]))
        if m != marked(c, start + MARKAT, name):
            raise Fail('%s: its mark is not the track\'s' % name)
        for x, g in curve:
            if abs(gainat(knots, x) / UNIT - min(1.0, max(0.0, g))) > LEVEL:
                raise Fail('%s: the knots give %.3f at %d where the recording gives %.3f'
                           % (name, gainat(knots, x) / UNIT, x, g))
        prove(f, c, (t, start, n, knots), first, slope, steady, name)


def wrap(items, lines, indent):
    line = indent
    for item in items:
        if len(line.expandtabs()) + len(item) > 72:
            lines.append(line.rstrip())
            line = indent
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


def c(cuts):
    """sys/cuts.c's text."""
    lines = ['#include <stddef.h>', '#include <stdint.h>', '#include "dat.h"', '#include "fns.h"', '',
             '/* Made by tools/cuts.py, which proves them. Do not edit. */', '']
    lines += comment([
        "FEATURE: the tunes the PC plays as XMIDI, cut from the CD's tracks "
        "as DotEmu cut them in 2014 for its recordings, LBA1-01 to "
        "LBA1-33, tune n's LBA1-(n+1). A cut is the samples of its track "
        "from its first, the PlayStation's tracks' sample, as many as n "
        "gives, each scaled by its gain, the 4096ths its knots draw from "
        "4096 before the first: the fade DotEmu drew, at the CD's level. "
        "Tunes 1, 3 to 9 and 32 are tracks whole, tunes 8, 9 and 32 track 10, "
        "and tunes 26 and 31 keep their XMIDI. The mark, the CRC-32 of the "
        "32 samples a second into the cut, finds its place on another "
        "CD's tracks. Taken from DotEmu 1.06."])
    lines.append('Cut cuts[Ntune] = {')
    for tune in range(NTUNE):
        cut = cuts[tune]
        if cut is None:
            continue
        t, first, n, knots, m = cut
        if not knots:
            lines.append('\t[%d] = {.track = %d},' % (tune, t))
            continue
        lines.append('\t[%d] = {%d, %d, %d, 0x%08xu, %d, {' % (tune, t, first, n, m, len(knots)))
        wrap(['{%d, %d},' % k for k in knots], lines, '\t\t')
        lines.append('\t}},')
    lines += ['};']
    return '\n'.join(lines) + '\n'


def usage():
    print('usage: cuts.py [-x pcm] [-d dir] [-t cd] [-c file]', file=sys.stderr)
    sys.exit(2)


def main(args):
    pcm = os.path.join(HERE, '..', 'build', 'linux-amd64-gcc', 'pcm')
    dir = os.path.join(TMP, 'steam', 'app', 'DotEmu', 'resources', 'LBA_FILES', 'MIDI_MP3', 'OGG')
    cd = os.path.join(TMP, 'pcmusic')
    against = None
    while args:
        if len(args) < 2 or args[0] not in ('-x', '-d', '-t', '-c'):
            usage()
        f, v = args.pop(0), args.pop(0)
        if f == '-x':
            pcm = v
        elif f == '-d':
            dir = v
        elif f == '-t':
            cd = v
        else:
            against = v
    try:
        if against is None:
            sys.stdout.write(c(made(pcm, dir, cd)))
        else:
            check(pcm, dir, cd, against)
    except (Fail, OSError) as e:
        print('cuts.py: %s' % e, file=sys.stderr)
        sys.exit(1)


main(sys.argv[1:])
