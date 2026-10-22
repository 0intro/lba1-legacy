"""framecmp: each of LBA.EXE's screens, as DOSBox showed it, among the port's.

    python3 -I tools/framecmp.py [-t tol] [-p share] [-l] frame.bmp... -- shot.ppm...

The port's frames are the 8-bit BMPs its -S writes, the screen's size
or, in the movies' mode, 320x200, with the DAC as their palette, all of
one size.
The shots are LBA.EXE's screens in DOSBox, sampled back to that size as
binary PPMs (magick shot.png -crop 948x711+166+124 +repage -filter
point -resize '640x480!' shot.ppm, or '320x200!'), DOSBox drawing both
modes in that area without blending. The port's frames are taken in the
order of
their screens' numbers (lbaN.bmp), a frame equal to the one before it
dropped. A shot is compared with every frame on one pixel in 64, then
in full with the three nearest: a pixel matches when its three channels
are within tol (default 1: DOSBox rounds the DAC's 6 bits to 8 where
the port shifts them) of the frame's, and the shot is found when its
best frame matches at least the share (default 0.99) of its pixels,
or, torn, when that share of
its pixels each match a frame or the one next to it: DOSBox shows a
frame the game is still writing, its first rows from one frame and the
rest, from a pixel of a row on, from the next, and a palette changed
while it is drawn the same way. Prints each shot's frame (or both)
and share, then how many were found, and exits 1 if one was not.

With -l the shots come from a screen that scales the frame to twice its
size linearly, as the Classic's does (SDL's linear filter), averaged
back by blocks of 2x2 (magick shot.png -scale 50% shot.ppm): such a
scale and average blur each pixel with its neighbours by 1, 6 and 1
eighths each way, and each frame is so blurred before it is compared,
its samples first and the three nearest whole.

So it was run on the dream that opens a new game (INTROD.FLA): the
port replaying a new game cut at the introduction's text, -S over the
movie's screens (those of 320x200, its frames 71 or 86 ms apart), and
LBA.EXE in DOSBox (tools/dosrun under tools/xkeys) shot every quarter
of a second from the same point. test/sweep runs it on every
scene, a lower share asked where the actors need not stand alike.
"""
import re
import struct
import sys

W, H = 0, 0  # the frames' size, the first's


def fail(msg):
    sys.stderr.write('framecmp: %s\n' % msg)
    sys.exit(1)


def bmp(path):
    """An 8-bit BMP's pixels as RGB bytes, top row first."""
    global W, H
    try:
        d = open(path, 'rb').read()
    except OSError as e:
        fail('%s: %s' % (path, e.strerror))
    if len(d) < 54 or d[:2] != b'BM':
        fail('%s: not a BMP' % path)
    off, = struct.unpack_from('<I', d, 10)
    hdr, = struct.unpack_from('<I', d, 14)
    w, h = struct.unpack_from('<ii', d, 18)
    bits, = struct.unpack_from('<H', d, 28)
    if W == 0:
        W, H = w, h
    if (w, h, bits) != (W, H, 8) or len(d) < off + W * H or off - 14 - hdr < 4 * 256:
        fail('%s: not %dx%d of 8 bits' % (path, W, H))
    pal = d[14 + hdr:off]
    rgb = [bytes((pal[4 * i + 2], pal[4 * i + 1], pal[4 * i])) for i in range(len(pal) // 4)]
    rows = [d[off + y * W:off + (y + 1) * W] for y in range(H)]
    return b''.join(b''.join(rgb[p] for p in row) for row in reversed(rows))


def ppm(path):
    """A binary PPM's pixels as RGB bytes, those after the white space
    byte that ends its header, which may be white space themselves."""
    try:
        d = open(path, 'rb').read()
    except OSError as e:
        fail('%s: %s' % (path, e.strerror))
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', d)
    if not m or (int(m.group(1)), int(m.group(2))) != (W, H) or len(d) != m.end() + W * H * 3:
        fail('%s: not a %dx%d PPM' % (path, W, H))
    return d[m.end():]


def sample(rgb, linear=False):
    """One pixel in 64, every eighth of every eighth row, blurred as
    blur blurs it with linear."""
    if not linear:
        return bytes(rgb[3 * (y * W + x) + c] for y in range(4, H, 8) for x in range(4, W, 8) for c in range(3))
    out = []
    for y in range(4, H, 8):
        for x in range(4, W, 8):
            for c in range(3):
                s = 0
                for dy, wy in ((-1, 1), (0, 6), (1, 1)):
                    for dx, wx in ((-1, 1), (0, 6), (1, 1)):
                        s += wy * wx * rgb[3 * ((y + dy) * W + x + dx) + c]
                out.append((s + 32) >> 6)
    return bytes(out)


def blur(rgb):
    """The frame as a linear scale to twice its size, averaged by blocks
    of 2x2, shows it: each channel blurred by 1, 6 and 1 eighths each
    way, the edges held."""
    rows = []
    n = 3 * W
    for y in range(H):
        r = rgb[n * y:n * (y + 1)]
        rows.append([(r[i - 3] if i >= 3 else r[i]) + 6 * r[i] + (r[i + 3] if i + 3 < n else r[i])
                     for i in range(n)])
    out = bytearray(n * H)
    for y in range(H):
        a, b, c = rows[max(y - 1, 0)], rows[y], rows[min(y + 1, H - 1)]
        out[n * y:n * (y + 1)] = bytes((a[i] + 6 * b[i] + c[i] + 32) >> 6 for i in range(n))
    return bytes(out)


def share(a, b, tol):
    """The share of pixels whose channels are all within tol."""
    n = 0
    for i in range(0, 3 * W * H, 3):
        if abs(a[i] - b[i]) <= tol and abs(a[i + 1] - b[i + 1]) <= tol and abs(a[i + 2] - b[i + 2]) <= tol:
            n += 1
    return n / (W * H)


def torn(rgb, a, b, tol):
    """The share of pixels whose channels are all within tol of a's or of b's."""
    n = 0
    for i in range(0, 3 * W * H, 3):
        if (abs(rgb[i] - a[i]) <= tol and abs(rgb[i + 1] - a[i + 1]) <= tol and abs(rgb[i + 2] - a[i + 2]) <= tol
                or abs(rgb[i] - b[i]) <= tol and abs(rgb[i + 1] - b[i + 1]) <= tol and abs(rgb[i + 2] - b[i + 2]) <= tol):
            n += 1
    return n / (W * H)


def number(path):
    m = re.search(r'(\d+)\D*$', path)
    return int(m.group(1)) if m else 0


def main():
    args = sys.argv[1:]
    tol, need, linear = 1, 0.99, False
    while args and args[0] in ('-t', '-p', '-l'):
        if args[0] == '-l':
            linear = True
            args = args[1:]
            continue
        if len(args) < 2:
            fail('%s needs a value' % args[0])
        if args[0] == '-t':
            tol = int(args[1])
        else:
            need = float(args[1])
        args = args[2:]
    if '--' not in args:
        sys.stderr.write('usage: framecmp [-t tol] [-p share] [-l] frame.bmp... -- shot.ppm...\n')
        sys.exit(2)
    i = args.index('--')
    frames = []
    for path in sorted(args[:i], key=number):
        rgb = bmp(path)
        if not frames or frames[-1][1] != rgb:
            frames.append((path, rgb, sample(rgb, linear)))
    if not frames:
        fail('no frames')
    blurred = {}

    def full(k):
        """Frame k's pixels as the shots show them."""
        if not linear:
            return frames[k][1]
        if k not in blurred:
            blurred[k] = blur(frames[k][1])
        return blurred[k]
    found = 0
    shots = args[i + 1:]
    for path in shots:
        rgb = ppm(path)
        s = sample(rgb)
        near = sorted(range(len(frames)), key=lambda k: sum(abs(x - y) for x, y in zip(s, frames[k][2])))[:3]
        best = max((share(rgb, full(k), tol), k) for k in near)
        what = '%s %.4f' % (frames[best[1]][0], best[0])
        ok = best[0] >= need
        if not ok:
            for k in near:
                for j in (k - 1, k + 1):
                    if ok or not 0 <= j < len(frames):
                        continue
                    a, b = min(j, k), max(j, k)
                    t = torn(rgb, full(a), full(b), tol)
                    if t >= need:
                        ok = True
                        what = '%s and %s %.4f, torn' % (frames[a][0], frames[b][0], t)
        found += ok
        print('%s: %s%s' % (path, what, '' if ok else ' not found'))
    print('%d of %d shots found among %d frames' % (found, len(shots), len(frames)))
    sys.exit(0 if found == len(shots) else 1)


main()
