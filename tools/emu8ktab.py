"""emu8ktab.py: the EMU8000's tables, as sys/emu8ktab.c holds them.

    python3 -I tools/emu8ktab.py > sys/emu8ktab.c

The chip (sys/emu8k.c) works in integers only, so that every build,
x87 or SSE, 32 or 64 bits, makes the same samples. What needs a
logarithm, an exponential, a cosine or a fit is worked out here once,
in doubles, and written as integers:

  emu8kexp2[4097]   2^(i/4096) in Q16, 65536 to 131072: the mantissa of
                    the chip's 1/4096-octave domains (pitch, attenuation).
  emu8klog2[4097]   4096*log2(1 + i/4096), 0 to 4096: a mantissa's
                    logarithm in 1/4096 octave, an amplitude's attenuation
                    from its top 13 bits.
  emu8kkernel[257][4]
                    the four-point interpolator's weights by the fraction's
                    top 8 bits (and 256 for the next sample), Q14, summing
                    to 16384: Catmull-Rom's, a placeholder for E-mu's
                    (US 5,111,727 describes the kind and leaves the
                    numbers open).
  emu8kfilter[16][256][3]
                    the two-pole low-pass for each Q (CCCA's top nibble) and
                    cutoff (its target's top byte, 125 Hz * 2^(6c/256)), a
                    state-variable filter (Chamberlin's, unity at DC): the
                    input's gain, its f and its q, Q28. Its poles are an
                    analogue two-pole's mapped by their impulse, its peak
                    above DC the ADIP's resonance table (5 to 28 dB at the
                    lowest cutoffs, flat to 18 dB at the highest, between
                    them linear in octaves), the input's gain the table's
                    loss at DC. At Q 0 and the top cutoff, f is 0: the
                    signal passes as it is.

`make tables` writes the file again and fails if it changed.
"""
import math
import sys

RATE = 44100.0
LOWQ = [5, 6, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23, 25, 26, 28]
HIGHQ = [0, 0.5, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 13, 15, 16, 18]
DCLOSS = [0, -0.5, -1.2, -1.8, -2.5, -3.3, -4.1, -5.5, -6.0, -6.6, -7.2, -7.9, -8.5, -9.3, -10.1, -11.0]


def cutoff(c):
    return 125.0 * 2.0 ** (6.0 * c / 256.0)


def poles(fc, q):
    """The analogue two-pole's poles at fc and Q, mapped to z by their impulse."""
    w0 = 2 * math.pi * min(fc, RATE * 0.45) / RATE
    r = math.exp(-w0 / (2 * q))
    th = w0 * math.sqrt(max(0.0, 1 - 1 / (4 * q * q)))
    return r, th


def gain(r, th, w):
    a1, a2 = -2 * r * math.cos(th), r * r
    re = 1 + a1 * math.cos(w) + a2 * math.cos(2 * w)
    im = -a1 * math.sin(w) - a2 * math.sin(2 * w)
    return 1.0 / math.hypot(re, im)


def peakdb(r, th):
    """The highest gain over the DC gain, at the peak's frequency in closed form."""
    c = (1 + r * r) * math.cos(th) / (2 * r)
    w = math.acos(max(-1.0, min(1.0, c)))
    return 20 * math.log10(max(gain(r, th, w), gain(r, th, 0.0)) / gain(r, th, 0.0))


def fit(fc, db):
    """The poles whose peak is db above DC at fc, Q by bisection from 0.5."""
    lo, hi = 0.5, 400.0
    for _ in range(80):
        mid = (lo + hi) / 2
        if peakdb(*poles(fc, mid)) < db:
            lo = mid
        else:
            hi = mid
    return poles(fc, (lo + hi) / 2)


def main():
    w = sys.stdout.write
    w("#include <stddef.h>\n#include <stdint.h>\n#include \"dat.h\"\n#include \"fns.h\"\n\n")
    w("/* Made by tools/emu8ktab.py, which says what each table is. Do not edit. */\n\n")
    w("uint32_t emu8kexp2[4097] = {\n")
    v = [round(65536 * 2 ** (i / 4096.0)) for i in range(4097)]
    for i in range(0, 4097, 8):
        w("\t" + ", ".join(str(x) for x in v[i:i+8]) + ",\n")
    w("};\n\n")
    w("uint16_t emu8klog2[4097] = {\n")
    v = [round(4096 * math.log2(1 + i / 4096.0)) for i in range(4097)]
    for i in range(0, 4097, 8):
        w("\t" + ", ".join(str(x) for x in v[i:i+8]) + ",\n")
    w("};\n\n")
    w("int16_t emu8kkernel[257][4] = {\n")
    for i in range(257):
        t = i / 256.0
        k = [(-t**3 + 2*t*t - t) / 2, (3*t**3 - 5*t*t + 2) / 2, (-3*t**3 + 4*t*t + t) / 2, (t**3 - t*t) / 2]
        q = [round(x * 16384) for x in k]
        q[1] += 16384 - sum(q)
        w("\t{%d, %d, %d, %d},\n" % tuple(q))
    w("};\n\n")
    w("int32_t emu8kfilter[16][256][3] = {\n")
    lo, hi = math.log2(cutoff(0)), math.log2(cutoff(255))
    for q in range(16):
        w("{\n")
        for c in range(256):
            fc = cutoff(c)
            x = (math.log2(fc) - lo) / (hi - lo)
            db = LOWQ[q] + (HIGHQ[q] - LOWQ[q]) * x
            if q == 0 and c == 255:
                g, f, qq = 1.0, 0.0, 0.0
            else:
                r, th = fit(fc, max(db, 0.0))
                f = math.sqrt(1 + r * r - 2 * r * math.cos(th))
                qq = (1 - r * r) / f
                g = 10 ** (DCLOSS[q] / 20.0)
            w("\t{%d, %d, %d},\n" % (round(g * (1 << 28)), round(f * (1 << 28)), round(qq * (1 << 28))))
        w("},\n")
    w("};\n")


if __name__ == "__main__":
    main()
