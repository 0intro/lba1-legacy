"""lst2sym.py: the procedures and data of a JWasm listing as a map of
the object at 0x10000, a line each: address, type (T a procedure, D a
datum), name and size.

    python3 -I tools/lst2sym.py listing

A procedure's size is the listing's. A datum's runs to the next name,
procedure or datum, or to the end of the last procedure, 0 past it, so
that each has the size tools/lecmp.py reads.
re/w_sb16.dll.sym is W_SB16.DLL's: the listing of lib386/lib_samp's
wave_a.asm and wave.inc as the tree holds them before the fixes of the
drivers' faults (SNAP_SIZE 8, as the CD's drivers have it), assembled
for the Sound Blaster 16 as the DOS build assembles it (jwasm with the
Makefile's DOSASFLAGS, -DSB16 -DSTEREO -DSAMPLE16BIT and -Fl), its
header added by hand.
"""
import re
import sys

BASE = 0x10000


def main():
    if len(sys.argv) != 2:
        sys.stderr.write('usage: lst2sym.py listing\n')
        sys.exit(2)
    procs, data, part = [], [], None
    for l in open(sys.argv[1], encoding='latin-1').read().split('\n'):
        if l.startswith('Procedures, parameters and locals:'):
            part = 'p'
        elif l.startswith('Symbols:'):
            part = 's'
        elif part == 'p':
            m = re.match(r'^(\w+) [ .]+P Near\s+([0-9A-F]+) _TEXT\s+([0-9A-F]+)', l)
            if m:
                procs.append((int(m.group(2), 16), 'T', m.group(1), int(m.group(3), 16)))
        elif part == 's':
            m = re.match(r'^(\w+) [ .]+(?:Byte|Word|DWord)(?:\[\d+\])?\s+([0-9A-F]+)h _TEXT', l)
            if m:
                data.append((int(m.group(2), 16), 'D', m.group(1)))
    if not procs:
        sys.exit('lst2sym.py: %s: no procedures' % sys.argv[1])
    end = max(a + n for a, _, _, n in procs)
    starts = sorted({a for a, _, _, _ in procs} | {a for a, _, _ in data} | {end})
    out = list(procs)
    for a, t, name in data:
        out.append((a, t, name, min((s for s in starts if s > a), default=a) - a))
    for a, t, name, n in sorted(out):
        print('0x%x %s %s %d' % (BASE + a, t, name, n))


if __name__ == '__main__':
    main()
