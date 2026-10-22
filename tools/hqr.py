"""hqr.py: list, expand and compare the entries of HQR files.

An HQR file is a table of 32-bit offsets, its count the first offset
over 4, each offset to an entry's 10-byte header (the size, the packed
size, the method: 0 stored, 1 LZS) and its data. An offset of 0 is an
empty entry. LZS is expand.asm's: a byte of flags, low bit first, 1 for a
byte copied, 0 for a word (the length less 2 in its low 4 bits, the
distance less 1 in its high 12). TEXT.HQR holds two entries a bank, 14
banks a language (English, French, German, Spanish, Italian): the texts'
numbers, then their offsets and bytes, in CP437.

    hqr.py -l file              each entry: index, method, size, packed
                                size, the CRC-32 of its bytes expanded
    hqr.py -c a b               the entries of a and b that differ
    hqr.py -t file              TEXT.HQR's texts: language, bank, number,
                                text (its newlines as \\n)
    hqr.py -x file n out        entry n's bytes expanded into out
"""
import os
import struct
import sys
import zlib

LANGS = ("EN", "FR", "DE", "SP", "IT")
BANKS = 14


def fail(msg):
    sys.stderr.write("%s: %s\n" % (os.path.basename(sys.argv[0]), msg))
    sys.exit(1)


def expand(src, size):
    """LZS's bytes expanded to size."""
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
                for k in range((w & 0xF) + 2):
                    out.append(out[start + k])
            flags >>= 1
    return bytes(out[:size])


def entries(path):
    """The file's entries: (method, size, packed size, bytes), None if empty."""
    with open(path, "rb") as f:
        b = f.read()
    if len(b) < 4:
        fail("%s: no table" % path)
    out = []
    # the table's last slot holds the file's size
    for i in range(struct.unpack_from("<I", b, 0)[0] // 4 - 1):
        off = struct.unpack_from("<I", b, 4 * i)[0]
        if off == 0 or off + 10 > len(b):
            out.append(None)
            continue
        size, packed, method = struct.unpack_from("<IIH", b, off)
        data = b[off + 10:off + 10 + packed]
        out.append((method, size, packed, data[:size] if method == 0 else expand(data, size)))
    return out


def texts(path):
    """TEXT.HQR's texts: (language, bank, number, text)."""
    e = entries(path)
    for lang in range(len(LANGS)):
        for bank in range(BANKS):
            i = (lang * BANKS + bank) * 2
            if i + 1 >= len(e) or e[i] is None or e[i + 1] is None:
                continue
            order, body = e[i][3], e[i + 1][3]
            for k, num in enumerate(struct.unpack_from("<%dH" % (len(order) // 2), order)):
                o0, o1 = struct.unpack_from("<HH", body, 2 * k)
                yield LANGS[lang], bank, num, body[o0:o1].rstrip(b"\0").decode("cp437")


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "-l":
        for i, x in enumerate(entries(a[1])):
            if x is None:
                print("%d\t-" % i)
            else:
                print("%d\t%d\t%d\t%d\t%08x" % (i, x[0], x[1], x[2], zlib.crc32(x[3])))
    elif len(a) == 3 and a[0] == "-c":
        ea, eb = entries(a[1]), entries(a[2])
        for i in range(max(len(ea), len(eb))):
            x = ea[i] if i < len(ea) else None
            y = eb[i] if i < len(eb) else None
            if x is None or y is None:
                if x is not y:
                    print("%d\t%s empty" % (i, a[1] if x is None else a[2]))
            elif x[3] != y[3]:
                n = sum(1 for p, q in zip(x[3], y[3]) if p != q) + abs(len(x[3]) - len(y[3]))
                print("%d\tsize %d and %d, %d bytes apart" % (i, x[1], y[1], n))
            elif x[:3] != y[:3]:
                print("%d\tthe same bytes, method %d and %d, packed %d and %d" % (i, x[0], y[0], x[2], y[2]))
        print("%d and %d entries" % (len(ea), len(eb)))
    elif len(a) == 2 and a[0] == "-t":
        for lang, bank, num, t in texts(a[1]):
            print("%s\t%d\t%d\t%s" % (lang, bank, num, t.replace("\t", " ").replace("\n", "\\n")))
    elif len(a) == 4 and a[0] == "-x":
        e = entries(a[1])
        n = int(a[2])
        if not 0 <= n < len(e) or e[n] is None:
            fail("%s: no entry %d" % (a[1], n))
        with open(a[3], "wb") as f:
            f.write(e[n][3])
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)


main()
