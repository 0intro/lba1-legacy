"""isocat.py: list or copy files out of an ISO 9660 image.

The image is either cooked, 2048 bytes a sector (the PC CD), or raw,
2352 bytes a sector as a cue/bin dump keeps it, its user data 16 bytes
into each sector in mode 1 (GOG's LBA.DOT) and 24 in mode 2 form 1 (the
PlayStation CD), or the cue sheet of such a dump, whatever its name
(GOG's LBA.DAT), whose files are BINARY and whose first holds the data
track, its FILE lines read as the port reads them (cuefile). A
directory entry may point past the data track into the audio tracks, as
the PlayStation disc's TRACKn.WAV and JINGLES/JnnMID.WAV do. Such a file
is listed, and copied from a cue sheet alone, which places the disc's
sectors in its files: as a WAV file, 16-bit stereo at 44100 Hz, the
entry's extent, a frame of 2352 bytes of samples for each 2048 bytes of
its size. On the PlayStation's disc that is the track from its INDEX 01
to the next track's pregap, where its executable stops playing it.

    isocat.py -l image                 list every file: size, sector, date, path
    isocat.py image path               write one file to standard output
    isocat.py -o dir image path...     copy files into dir, by base name,
                                       a directory's files for a directory
    isocat.py -r dir image [path...]   copy the files below each path (the
                                       root by default) into dir, each under
                                       its path on the disc, dated as recorded

A date is the directory entry's, written as the disc gives it, its local
time and its offset from UTC. -r sets each file's modification time to
that instant.
"""
import calendar
import os
import re
import struct
import sys

COOKED = 2048
RAW = 2352


def fail(msg):
    sys.stderr.write("%s: %s\n" % (os.path.basename(sys.argv[0]), msg))
    sys.exit(1)


def cuelines(cue):
    """A cue sheet's lines, stripped, a UTF-8 byte order mark dropped."""
    for line in open(cue, encoding="latin-1"):
        yield line.replace("\xef\xbb\xbf", "", 1).strip()


def cuefileline(w):
    """A FILE line's name and kind, the keyword in any case, or None."""
    return re.match(r'FILE\s+"(.*)"\s+(\S+)$', w, re.I) or re.match(r'FILE\s+(\S+)\s+(\S+)$', w, re.I)


def cuefile(base, name):
    """The path of a FILE line's name, its \\ read as /, found beside the
    sheet, base, or from the root, each part whatever its case, as the
    port finds it. One missing is said in a line."""
    name = name.replace("\\", "/")
    path = "/" if name.startswith("/") else base or "."
    for part in name.split("/"):
        if part in ("", "."):
            continue
        p = os.path.join(path, part)
        if not os.path.exists(p):
            try:
                p = next(os.path.join(path, e) for e in sorted(os.listdir(path)) if e.lower() == part.lower())
            except (OSError, StopIteration):
                fail("%s: no such file beside %s" % (name, base or "."))
        path = p
    return path


def cuefiles(cue):
    """The files a cue sheet names, in order: (path, frames of 2352 bytes)."""
    base = os.path.dirname(cue)
    out = []
    for w in cuelines(cue):
        m = cuefileline(w)
        if m:
            if m.group(2).upper() != "BINARY":
                fail("%s: only BINARY files" % m.group(1))
            path = cuefile(base, m.group(1))
            out.append((path, os.path.getsize(path) // RAW))
    if not out:
        fail("%s: no FILE" % cue)
    return out


def cuedata(cue):
    """The sectors of the data track where the first file holds an audio
    track too, its first INDEX, or None for a file of its own."""
    nfile, audio = 0, False
    for w in cuelines(cue):
        if cuefileline(w):
            nfile += 1
            continue
        m = re.match(r"TRACK\s+\d+\s+(\S+)", w, re.I)
        if m:
            audio = m.group(1).upper() == "AUDIO"
            continue
        m = re.match(r"INDEX\s+\d+\s+(\d+):(\d+):(\d+)", w, re.I)
        if m and audio:
            if nfile != 1:
                return None
            return (int(m.group(1)) * 60 + int(m.group(2))) * 75 + int(m.group(3))
    return None


def cuesheet(path):
    """Whether the file at path is a cue sheet: a FILE line among its first bytes."""
    with open(path, "rb") as f:
        head = f.read(4096)
    return re.search(rb"^[ \t]*FILE[ \t]", head, re.M | re.I) is not None


class Image:
    """A data track: its sector size and the offset of user data in a
    sector, and from a cue sheet the files holding the disc's frames."""

    def __init__(self, path):
        self.files = None
        self.data = None
        if path.lower().endswith(".cue") or cuesheet(path):
            self.files = cuefiles(path)
            self.data = cuedata(path)
            path = self.files[0][0]
        self.f = open(path, "rb")
        self.size = os.fstat(self.f.fileno()).st_size
        for secsize, skip in ((COOKED, 0), (RAW, 24), (RAW, 16)):
            self.secsize = secsize
            self.skip = skip
            if self.sector(16)[1:6] == b"CD001":
                if self.files is not None and secsize != RAW:
                    fail("%s: a cue sheet's data track in sectors of 2352 bytes" % path)
                return
        fail("%s: no ISO 9660 volume descriptor" % path)

    def nsectors(self):
        return self.data if self.data is not None else self.size // self.secsize

    def sector(self, lba):
        self.f.seek(lba*self.secsize + self.skip)
        return self.f.read(COOKED)

    def read(self, lba, n):
        b = bytearray()
        for i in range((n+COOKED-1) // COOKED):
            b += self.sector(lba+i)
        return bytes(b[:n])

    def frames(self, lba, n):
        """The disc's n frames from sector lba, whole, across the files."""
        b = bytearray()
        start = 0
        for path, count in self.files:
            first = max(lba, start)
            k = min(lba + n, start + count) - first
            if k > 0:
                with open(path, "rb") as f:
                    f.seek((first - start) * RAW)
                    b += f.read(k * RAW)
            start += count
        if len(b) != n * RAW:
            fail("sectors %d to %d: past the files' end" % (lba, lba + n - 1))
        return bytes(b)


def date(rec):
    """A directory entry's seven bytes of date: (text, seconds since 1970
    UTC), or a text of zeros and None where the disc records no date or
    one past ECMA-119's ranges."""
    y, mo, d, h, mi, s, gmt = struct.unpack("<6Bb", rec)
    if mo < 1 or mo > 12 or d < 1 or d > 31 or h > 23 or mi > 59 or s > 59 or gmt < -48 or gmt > 52:
        return "0000-00-00 00:00:00+00:00", None
    off = gmt * 15
    text = "%04d-%02d-%02d %02d:%02d:%02d%s%02d:%02d" % (
        1900+y, mo, d, h, mi, s, "-" if off < 0 else "+", abs(off) // 60, abs(off) % 60)
    t = calendar.timegm((1900+y, mo, d, h, mi, s, 0, 0, 0)) - off*60
    return text, t


def walk(img, lba, size, path, out):
    """Append (path, lba, size, date) for every file below the directory at lba."""
    d = img.read(lba, size)
    o = 0
    while o < len(d):
        n = d[o]
        if n == 0:
            o = (o//COOKED + 1) * COOKED
            continue
        elba, esize = struct.unpack_from("<I4xI", d, o+2)
        rec = d[o+18:o+25]
        flags = d[o+25]
        name = d[o+33:o+33+d[o+32]]
        o += n
        if name in (b"\0", b"\1"):
            continue
        name = name.decode("latin-1").split(";")[0]
        if flags & 2:
            walk(img, elba, esize, path+name+"/", out)
        else:
            out.append((path+name, elba, esize, date(rec)))


def files(img):
    pvd = img.sector(16)
    lba, size = struct.unpack_from("<I4xI", pvd, 156+2)
    out = []
    walk(img, lba, size, "/", out)
    return out


def select(entries, path):
    """The file at path, or the files below the directory at path."""
    want = path.upper().rstrip("/")
    if not want.startswith("/"):
        want = "/" + want
    if want == "/":
        return list(entries)
    for e in entries:
        if e[0].upper() == want:
            return [e]
    below = [e for e in entries if e[0].upper().startswith(want + "/")]
    if not below:
        fail("%s: no such file" % path)
    return below


def outside(img, e):
    path, lba, size, when = e
    return lba + (size+COOKED-1)//COOKED > img.nsectors()


def wav(samples):
    """samples, 16-bit stereo at 44100 Hz, with a WAV file's header."""
    n = len(samples)
    return (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVE"
            + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, 44100, 44100 * 4, 4, 16)
            + b"data" + struct.pack("<I", n) + samples)


def content(img, e):
    path, lba, size, when = e
    if not outside(img, e):
        return img.read(lba, size)
    if img.files is None:
        fail("%s: past the end of the data track (an audio track?): give the cue sheet" % path)
    return wav(img.frames(lba, (size+COOKED-1) // COOKED))


def main(args):
    outdir = None
    tree = False
    listing = False
    while args and args[0].startswith("-"):
        if args[0] == "-l":
            listing = True
            args = args[1:]
        elif args[0] in ("-o", "-r") and len(args) > 1:
            tree = args[0] == "-r"
            outdir = args[1]
            args = args[2:]
        else:
            fail("usage: isocat.py [-l] [-o dir | -r dir] image [path...]")
    if not args:
        fail("usage: isocat.py [-l] [-o dir | -r dir] image [path...]")
    img = Image(args[0])
    entries = files(img)
    if listing:
        for e in entries:
            note = ""
            if outside(img, e):
                note = "\toutside the data track"
            print("%d\t%d\t%s\t%s%s" % (e[2], e[1], e[3][0], e[0], note))
        return
    if outdir is None:
        if len(args) != 2:
            fail("one path to write to standard output")
        e = select(entries, args[1])
        if len(e) != 1:
            fail("%s: a directory" % args[1])
        sys.stdout.buffer.write(content(img, e[0]))
        return
    paths = args[1:]
    if tree and not paths:
        paths = ["/"]
    os.makedirs(outdir, exist_ok=True)
    for p in paths:
        for e in select(entries, p):
            if any(q in ("", ".", "..") for q in e[0].lstrip("/").split("/")):
                fail("%s: a name that leaves the output directory" % e[0])
            if tree:
                out = os.path.join(outdir, *e[0].lstrip("/").split("/"))
                os.makedirs(os.path.dirname(out), exist_ok=True)
            else:
                out = os.path.join(outdir, os.path.basename(e[0]))
            with open(out, "wb") as f:
                f.write(content(img, e))
            if tree and e[3][1] is not None:
                os.utime(out, (e[3][1], e[3][1]))


if __name__ == "__main__":
    main(sys.argv[1:])
