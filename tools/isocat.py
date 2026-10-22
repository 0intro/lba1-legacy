"""isocat.py: list or copy files out of an ISO 9660 image.

The image is either cooked, 2048 bytes a sector (the PC CD), or raw, 2352
bytes a sector as a cue/bin dump keeps it (the PlayStation CD, mode 2 form
1, whose user data starts 24 bytes into each sector). Only the data track
is in the file: a directory entry that points past its end, as the
PlayStation disc's TRACKn.WAV do into the audio tracks, is listed, and a
copy of it fails. cuelines, cuefileline and cuefile read a cue sheet as the
port reads it, for tools/cdtracks.py.

    isocat.py -l image                 list every file: size, sector, date, path
    isocat.py image path               write one file to standard output
    isocat.py -o dir image path...     copy files into dir, by base name

A date is the directory entry's, written as the disc gives it, its local
time and its offset from UTC.
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


class Image:
    """A data track: its sector size and the offset of user data in a sector."""

    def __init__(self, path):
        self.f = open(path, "rb")
        self.size = os.fstat(self.f.fileno()).st_size
        for secsize, skip in ((COOKED, 0), (RAW, 24), (RAW, 16)):
            self.secsize = secsize
            self.skip = skip
            if self.sector(16)[1:6] == b"CD001":
                return
        fail("%s: no ISO 9660 volume descriptor" % path)

    def nsectors(self):
        return self.size // self.secsize

    def sector(self, lba):
        self.f.seek(lba*self.secsize + self.skip)
        return self.f.read(COOKED)

    def read(self, lba, n):
        b = bytearray()
        for i in range((n+COOKED-1) // COOKED):
            b += self.sector(lba+i)
        return bytes(b[:n])


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


def find(entries, path):
    want = path.upper()
    if not want.startswith("/"):
        want = "/" + want
    for e in entries:
        if e[0].upper() == want:
            return e
    fail("%s: no such file" % path)


def content(img, e):
    path, lba, size, when = e
    if lba + (size+COOKED-1)//COOKED > img.nsectors():
        fail("%s: past the end of the data track (an audio track?)" % path)
    return img.read(lba, size)


def main(args):
    outdir = None
    listing = False
    while args and args[0].startswith("-"):
        if args[0] == "-l":
            listing = True
            args = args[1:]
        elif args[0] == "-o" and len(args) > 1:
            outdir = args[1]
            args = args[2:]
        else:
            fail("usage: isocat.py [-l] [-o dir] image [path...]")
    if not args:
        fail("usage: isocat.py [-l] [-o dir] image [path...]")
    img = Image(args[0])
    entries = files(img)
    if listing:
        for path, lba, size, when in entries:
            note = ""
            if lba + (size+COOKED-1)//COOKED > img.nsectors():
                note = "\toutside the data track"
            print("%d\t%d\t%s\t%s%s" % (size, lba, when[0], path, note))
        return
    if outdir is None:
        if len(args) != 2:
            fail("one path to write to standard output")
        sys.stdout.buffer.write(content(img, find(entries, args[1])))
        return
    os.makedirs(outdir, exist_ok=True)
    for p in args[1:]:
        e = find(entries, p)
        with open(os.path.join(outdir, os.path.basename(e[0])), "wb") as f:
            f.write(content(img, e))


if __name__ == "__main__":
    main(sys.argv[1:])
