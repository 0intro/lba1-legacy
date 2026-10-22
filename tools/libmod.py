"""libmod: a module of an OMF library, written out as an object.

    python3 -I tools/libmod.py [-l] lib module > obj

lib is a library of Intel's OMF, as Watcom's wlib makes them. Its
records are walked from its header, each module from its first THEADR
or LHEADR to its MODEND (MASM writes a THEADR for each file a module
includes), and the module whose first header names it, its case
ignored, is written whole to standard output: an object the linker
takes as it took the library's member. -l lists the module's public and
external names instead.

The DOS builds, make's CC=openwatcom and CC=watcom, take from it the
module qsort of Watcom C/C++ 10.0a's register-call DOS library,
CLIB3R.LIB, which LBA.EXE links first.
"""
import struct
import sys


def modules(b):
    """(name, start, end) of each module of the library b."""
    page = 16
    i = 0
    if b[0] == 0xF0:
        page = struct.unpack_from('<H', b, 1)[0] + 3
        i = page
    name = None
    start = None
    while i + 3 <= len(b):
        t = b[i]
        n = struct.unpack_from('<H', b, i + 1)[0]
        if t == 0xF1:
            break
        if t in (0x80, 0x82) and start is None:
            start = i
            name = b[i + 4:i + 4 + b[i + 3]].decode('latin-1')
        i += 3 + n
        if t in (0x8A, 0x8B) and start is not None:
            yield name, start, i
            start = None
            if i % page:
                i += page - i % page


def index(body, j):
    """The offset past the OMF index at j."""
    return j + (2 if body[j] & 0x80 else 1)


def names(obj):
    """The public and external names of the object obj."""
    pub = []
    ext = []
    i = 0
    while i + 3 <= len(obj):
        t = obj[i]
        n = struct.unpack_from('<H', obj, i + 1)[0]
        body = obj[i + 3:i + 3 + n - 1]
        if t in (0x8C, 0xB4):
            j = 0
            while j < len(body):
                k = body[j]
                ext.append(body[j + 1:j + 1 + k].decode('latin-1'))
                j = index(body, j + 1 + k)
        elif t in (0x90, 0x91, 0xB6, 0xB7):
            j = index(body, 0)
            seg = body[j]
            j = index(body, j)
            if seg == 0:
                j += 2
            while j < len(body):
                k = body[j]
                pub.append(body[j + 1:j + 1 + k].decode('latin-1'))
                j = index(body, j + 1 + k + (4 if t & 1 else 2))
        i += 3 + n
    return pub, ext


def main():
    args = sys.argv[1:]
    listing = args[:1] == ['-l']
    if listing:
        args = args[1:]
    if len(args) != 2:
        sys.stderr.write('usage: libmod.py [-l] lib module > obj\n')
        sys.exit(2)
    b = open(args[0], 'rb').read()
    for name, start, end in modules(b):
        if name is not None and name.lower() == args[1].lower():
            obj = b[start:end]
            if listing:
                pub, ext = names(obj)
                print('public: ' + ' '.join(pub))
                print('external: ' + ' '.join(ext))
            else:
                sys.stdout.buffer.write(obj)
            return
    sys.stderr.write('libmod.py: no module %s in %s\n' % (args[1], args[0]))
    sys.exit(1)


main()
