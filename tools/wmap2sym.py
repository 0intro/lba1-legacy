"""wmap2sym.py: turn a Watcom linker map into a symbol map.

    wmap2sym.py file.map file.exe > file.sym

Each symbol of the map's memory map, the statics among them when the map
lists them (wlink's option statics), becomes a line of the form
le2elf.py and lecmp.py read:

    0xVA type name size

The address is the symbol's object offset plus the object's base address,
read from the LE or LX header of the linked file. type is T in a segment
of class CODE and D elsewhere. The size runs to the next symbol of the
same object, or to the end of its last segment. Absolute symbols (object
0, such as __DOSseg__) are left out.
"""
import os
import re
import struct
import sys

# Watcom 9 leaves a code segment's group empty, where OpenWatcom writes
# AUTO, and names a module on the line of its first symbol, where
# OpenWatcom gives the module a line of its own
SEGMENT = re.compile(r"^(\S+)\s+(\S+)\s+(?:\S+\s+)?([0-9a-f]{4}):([0-9a-f]{8})\s+([0-9a-f]{8})\s*$")
SYMBOL = re.compile(r"^([0-9a-f]{4}):([0-9a-f]{8})[*+s ]?\s+(\S+)(?:\s+\S+)?\s*$")


def fail(msg):
    sys.stderr.write("%s: %s\n" % (os.path.basename(sys.argv[0]), msg))
    sys.exit(1)


def bases(path):
    b = open(path, "rb").read()
    if b[:2] != b"MZ":
        fail("%s: not an MZ executable" % path)
    h = struct.unpack_from("<I", b, 0x3c)[0]
    if b[h:h+2] not in (b"LE", b"LX"):
        fail("%s: not an LE or LX image" % path)
    tab = h + struct.unpack_from("<I", b, h+0x40)[0]
    n = struct.unpack_from("<I", b, h+0x44)[0]
    return [struct.unpack_from("<I", b, tab + 24*i + 4)[0] for i in range(n)]


def main(args):
    if len(args) != 2:
        fail("usage: wmap2sym.py file.map file.exe")
    base = bases(args[1])
    code = set()
    end = {}
    syms = []
    insyms = False
    for line in open(args[0], encoding="latin-1"):
        m = SEGMENT.match(line)
        if m and not insyms:
            obj = int(m.group(3), 16)
            off = int(m.group(4), 16)
            size = int(m.group(5), 16)
            if m.group(2) == "CODE":
                code.add(obj)
            end[obj] = max(end.get(obj, 0), off + size)
            continue
        if line.startswith("Address        Symbol"):
            insyms = True
            continue
        if insyms:
            m = SYMBOL.match(line)
            if m:
                syms.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    syms = sorted(s for s in syms if s[0] != 0)
    for i, (obj, off, name) in enumerate(syms):
        nxt = end.get(obj, off)
        if i + 1 < len(syms) and syms[i+1][0] == obj:
            nxt = syms[i+1][1]
        if obj < 1 or obj > len(base):
            fail("%s: object %d not in %s" % (name, obj, args[1]))
        print("%#x %s %s %d" % (base[obj-1] + off, "T" if obj in code else "D", name, nxt - off))


if __name__ == "__main__":
    main(sys.argv[1:])
