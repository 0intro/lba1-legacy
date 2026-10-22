"""cdtracks.py: a CD image's audio tracks as WAV files, as the port plays them.

    cdtracks.py image.cue dir [track[=as]...]

Writes each audio track of the cue sheet, or those listed, into dir as
trackNN.wav (NN its number, or the one given after =): 16-bit stereo at
44100 Hz, from the track's INDEX 01 to the next track's INDEX 01, or to
the image's end, which is the extent a CD's table of contents gives a
track and LBA's CD driver played. The next track's PREGAP lies within it
as silence, before that track's INDEX 00, else its INDEX 01, as on the
disc. The sheet, read as the port reads it (tools/isocat.py's cuefile),
may name one BINARY file for the whole disc or one per track (as
Redump's do), each track in the file of its INDEX 01, and its INDEX 00
there or at the end of the file of the track before (as Exact Audio Copy
appends the gaps). The samples are raw CD audio, 2352 bytes a frame,
little-endian as BINARY says, unless they step less than a quarter as
much read big-endian, from each sample to the next of its channel, at 16
places of each audio track, as the port reads them (sys/cdimage.c's
bigendian): each sample's bytes, the sounds' too, are then swapped as
written. Prints each file written and its length in frames.

Given no track, a disc whose first file starts with its data track in
sectors of 2352 bytes and whose root names its tunes, TRACK1.WAV at an
audio track's INDEX 01 as on the PlayStation's, is written as the port
reads it as a CD: the track of each TRACKn.WAV as trackNN.wav, NN = n+1,
the PC CD's number of tune n, and every other file of its directory
whose extent lies in an audio track at its own path, JINGLES/J01MID.WAV
and the rest, as tools/isocat.py copies it: the entry's extent, a frame
of samples for each 2048 bytes of its size. Such a disc holds no PREGAP.

The PC CD's music is its tracks 2 to 10. The PlayStation's disc carries
the same tunes as its tracks 2 to 9 and 27, with its recordings of the
jingles:

    cdtracks.py "Little Big Adventure (Europe) (En,Fr,De,Es,It).cue" ../lba1-tmp/psmusic
"""
import importlib.util
import os
import re
import struct
import sys

FRAME = 2352
NPROBE = 16
PROBE = 4

# tools/isocat.py, the reader of the disc's directory, loaded by its
# path, as python3 -I leaves the tools out of the path
_spec = importlib.util.spec_from_file_location(
    "isocat", os.path.join(os.path.dirname(os.path.abspath(__file__)), "isocat.py"))
isocat = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(isocat)


def fail(msg):
    sys.stderr.write("cdtracks.py: %s\n" % msg)
    sys.exit(1)


def msf(s):
    m, sec, f = (int(x) for x in s.split(":"))
    return (m * 60 + sec) * 75 + f


def parse(cue):
    """The sheet's tracks: (number, audio, INDEX 01's file, INDEX 01, INDEX 00 or None, PREGAP, INDEX 00's file, kind), in frames,
    and its files in order."""
    base = os.path.dirname(cue)
    tracks = []
    files = []
    path = None
    for w in isocat.cuelines(cue):
        m = isocat.cuefileline(w)
        if m:
            if m.group(2).upper() != "BINARY":
                fail("%s: only BINARY files" % m.group(1))
            path = isocat.cuefile(base, m.group(1))
            files.append(path)
            continue
        m = re.match(r"TRACK\s+(\d+)\s+(\S+)$", w, re.I)
        if m:
            if path is None:
                fail("a TRACK before any FILE")
            kind = m.group(2).upper()
            tracks.append([int(m.group(1)), kind == "AUDIO", None, None, None, 0, None, kind])
            continue
        m = re.match(r"PREGAP\s+(\d+:\d+:\d+)$", w, re.I)
        if m:
            if not tracks:
                fail("a PREGAP before any TRACK")
            if tracks[-1][3] is not None or tracks[-1][4] is not None:
                fail("track %d: a PREGAP after its INDEX" % tracks[-1][0])
            tracks[-1][5] = msf(m.group(1))
            continue
        m = re.match(r"INDEX\s+0([01])\s+(\d+:\d+:\d+)$", w, re.I)
        if m and tracks:
            if m.group(1) == "0":
                tracks[-1][4], tracks[-1][6] = msf(m.group(2)), path
            else:
                tracks[-1][2], tracks[-1][3] = path, msf(m.group(2))
    for t in tracks:
        if t[3] is None:
            fail("track %d: no INDEX 01" % t[0])
    return tracks, files


def extent(tracks, i):
    """The pieces of track i: (file, first frame, frames) up to the next INDEX 01, None the silence of its PREGAP."""
    path, start = tracks[i][2], tracks[i][3]
    end = os.path.getsize(path) // FRAME
    if i + 1 == len(tracks):
        return [(path, start, end - start)]
    nxt = tracks[i + 1]
    atpath, at = (nxt[6], nxt[4]) if nxt[4] is not None else (nxt[2], nxt[3])
    if atpath == path:
        pieces = [(path, start, at - start), (None, 0, nxt[5])]
        if nxt[2] == path:
            pieces.append((path, at, nxt[3] - at))
        else:
            pieces += [(path, at, end - at), (nxt[2], 0, nxt[3])]
    elif atpath == nxt[2]:
        pieces = [(path, start, end - start), (nxt[2], 0, at), (None, 0, nxt[5]), (nxt[2], at, nxt[3] - at)]
    else:
        fail("track %d: its INDEX 00 in neither its file nor the file of the track before" % nxt[0])
    return [p for p in pieces if p[2] > 0]


def addresses(tracks):
    """Each track's INDEX 01 among the disc's sectors: the frames of the files
    before its own, its INDEX 01 in its file and the PREGAPs so far."""
    base, seen, gaps, out = {}, 0, 0, []
    for t in tracks:
        for p in (t[6], t[2]):
            if p is not None and p not in base:
                base[p] = seen
                seen += os.path.getsize(p) // FRAME
        gaps += t[5]
        out.append(base[t[2]] + t[3] + gaps)
    return out


def tunes(cue, tracks, files):
    """The disc's tunes as its root names them and its other sounds: the
    tracks' indexes by tune, and the entries whose extents lie in an audio
    track, or None where its first file starts with no data track of 2352
    bytes a sector, which tools/isocat.py reads, or its TRACK1.WAV starts
    no audio track."""
    t = tracks[0]
    if t[1] or t[7] not in ("MODE1/2352", "MODE2/2352") or t[2] != files[0] or t[3] != 0:
        return None
    img = isocat.Image(cue)
    data = [i for i, t in enumerate(tracks) if not t[1]]
    addr = addresses(tracks)
    at, span = {}, []
    for i, t in enumerate(tracks):
        if t[1]:
            at[addr[i] - addr[data[0]]] = i
            span.append((addr[i] - addr[data[0]], sum(p[2] for p in extent(tracks, i))))
    named, sounds = {}, []
    for e in isocat.files(img):
        path, lba, size, when = e
        m = re.match(r"/TRACK(\d+)\.WAV$", path, re.I)
        if m and lba in at:
            named[int(m.group(1))] = at[lba]
            continue
        n = (size + isocat.COOKED - 1) // isocat.COOKED
        if n > 0 and any(s <= lba and lba + n <= s + k for s, k in span):
            sounds.append(e)
    if 1 not in named:
        return None
    if any(t[5] for t in tracks):
        fail("a disc that names its tunes with a PREGAP")
    return img, named, sounds


def step(a, b):
    """The step from the sample a to the sample b, each given by its 16
    bits."""
    return abs(((a ^ 0x8000) - 0x8000) - ((b ^ 0x8000) - 0x8000))


def bigendian(tracks):
    """Whether the sheet's samples are big-endian: their steps from each
    sample to the next of its channel, summed over PROBE frames at NPROBE
    places of each audio track, less than a quarter of the same bytes'
    read little-endian."""
    le = be = 0
    for i, t in enumerate(tracks):
        if not t[1]:
            continue
        path, start = t[2], t[3]
        if i + 1 < len(tracks) and tracks[i + 1][2] == path:
            span = tracks[i + 1][3] - start
        else:
            span = os.path.getsize(path) // FRAME - start
        with open(path, "rb") as f:
            for p in range(NPROBE):
                f.seek((start + span * (2 * p + 1) // (2 * NPROBE)) * FRAME)
                b = f.read(PROBE * FRAME)
                for k in range(0, len(b) - 5, 2):
                    le += step(b[k] | b[k + 1] << 8, b[k + 4] | b[k + 5] << 8)
                    be += step(b[k] << 8 | b[k + 1], b[k + 4] << 8 | b[k + 5])
    return be * 4 < le


def put(out, fill):
    """out written by fill, given the file to write, under the name
    out.new, renamed out once whole: a failure, said, leaves neither."""
    tmp = out + ".new"
    try:
        with open(tmp, "wb") as w:
            fill(w)
        os.replace(tmp, out)
    except (OSError, ValueError) as e:
        try:
            os.remove(tmp)
        except OSError:
            pass
        fail("%s: %s" % (out, e))


def write(pieces, out, swap):
    n = sum(p[2] for p in pieces)

    def fill(w):
        w.write(b"RIFF" + struct.pack("<I", 36 + n * FRAME) + b"WAVE")
        w.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, 44100, 44100 * 4, 4, 16))
        w.write(b"data" + struct.pack("<I", n * FRAME))
        for path, first, count in pieces:
            if path is None:
                w.write(bytes(count * FRAME))
                continue
            with open(path, "rb") as f:
                f.seek(first * FRAME)
                left = count * FRAME
                while left > 0:
                    b = f.read(min(left, 1 << 20))
                    if not b:
                        raise ValueError("%s short" % path)
                    if swap:
                        b = bytearray(b)
                        b[0::2], b[1::2] = b[1::2], b[0::2]
                    w.write(b)
                    left -= len(b)

    put(out, fill)
    return n


def main(args):
    if len(args) < 2:
        fail("usage: cdtracks.py image.cue dir [track[=as]...]")
    tracks, files = parse(args[0])
    swap = bigendian(tracks)
    want = {}
    for a in args[2:]:
        m = re.match(r"(\d+)(?:=(\d+))?$", a)
        if not m:
            fail("bad track %s" % a)
        want[int(m.group(1))] = int(m.group(2) or m.group(1))
    disc = None if want else tunes(args[0], tracks, files)
    if disc is not None:
        img, named, sounds = disc
        for n, i in sorted(named.items()):
            out = os.path.join(args[1], "track%02d.wav" % (n + 1))
            print("%s\t%d frames" % (out, write(extent(tracks, i), out, swap)))
        for e in sounds:
            out = os.path.join(args[1], *e[0].strip("/").split("/"))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            b = isocat.content(img, e)
            if swap:
                b = bytearray(b)
                b[44::2], b[45::2] = b[45::2], b[44::2]
            put(out, lambda w: w.write(b))
            print("%s\t%d frames" % (out, (len(b) - 44) // FRAME))
        return
    for i, t in enumerate(tracks):
        if not t[1] or (want and t[0] not in want):
            continue
        out = os.path.join(args[1], "track%02d.wav" % want.get(t[0], t[0]))
        n = write(extent(tracks, i), out, swap)
        print("%s\t%d frames" % (out, n))
        want.pop(t[0], None)
    for t in want:
        fail("no audio track %d" % t)


if __name__ == "__main__":
    main(sys.argv[1:])
