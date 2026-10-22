"""loudness.py: the loudness of sound recordings, second by second.

    loudness.py [-c] [-w seconds] a.raw [b.raw...]

The recordings are what tools/dosrun -r writes: DOSBox's mixer output,
48 kHz stereo 32-bit float. A line per window (one second by default)
gives its start and the RMS of each recording, full scale 1, side by
side, so two runs of the same scenes can be read against each other:
the same sounds at the same moments, give or take the moments keys
arrived. With -c each recording gives two columns, the RMS of its left
channel, then of its right.
"""
import array
import math
import operator
import sys

RATE = 48000


def fail(msg):
    sys.stderr.write("loudness.py: %s\n" % msg)
    sys.exit(1)


def rms(w):
    return math.sqrt(sum(map(operator.mul, w, w)) / len(w))


def loudness(path, win, apart):
    a = array.array("f")
    with open(path, "rb") as f:
        b = f.read()
    a.frombytes(b[:len(b) - len(b) % a.itemsize])
    if sys.byteorder != "little":
        a.byteswap()
    if apart:
        n = 2 * int(RATE * win)
    else:
        n = int(RATE * 2 * win)
    out = []
    for i in range(0, len(a) - n + 1, n):
        w = a[i:i+n]
        if apart:
            out.append((rms(w[0::2]), rms(w[1::2])))
        else:
            out.append((rms(w),))
    return out


def main(args):
    win = 1.0
    apart = False
    while args and args[0] in ("-c", "-w"):
        if args[0] == "-c":
            apart = True
            args = args[1:]
        elif len(args) > 1:
            win = float(args[1])
            args = args[2:]
        else:
            break
    if not args or args[0].startswith("-") or win <= 0:
        fail("usage: loudness.py [-c] [-w seconds] a.raw [b.raw...]")
    ls = [loudness(p, win, apart) for p in args]
    empty = "\t".join("-" * (2 if apart else 1))
    for i in range(max(len(l) for l in ls)):
        cols = ["\t".join("%.4f" % v for v in l[i]) if i < len(l) else empty for l in ls]
        print("%6.1f\t%s" % (i * win, "\t".join(cols)))


if __name__ == "__main__":
    main(sys.argv[1:])
