"""grids.py: the scenes' brick grids of LBA_GRI.HQR read, compared and
mapped, and their layouts of LBA_BLL.HQR.

    grids.py -d a.hqr b.hqr [entry...]
    grids.py -m gri.hqr entry x0 x1 z0 z1
    grids.py -l bll.hqr gri.hqr entry [layout...]
    grids.py -t eu.hqr us.hqr dos.hqr
    grids.py -c file eu.hqr us.hqr dos.hqr

A grid (entry n of LBA_GRI.HQR, scene n's) is 64 by 64 columns, x
across and z down, each 25 cells high from y 0, as grille.c's
CopyMapToCube unpacks them: a word a column, x + 64 z, the offset of
its packed cells, then the grid's last 32 bytes, a bit a layout from
the high bit of the first byte, the layouts its cells use, whose
bricks InitGrille loads. Entries from 120 on are the patches that
LM_SET_GRM and the zones of type 3 lay over a scene's grid
(IncrustGrm), packed as a grid. A cell is a layout, from 1, and a brick
of it, the layout's x + dx (y + dy z), written layout.brick, and . for
an empty cell. A cell of layout 0 is drawn as nothing and collides as
its brick's byte says, 1 a full brick (grille_a.c's brick): Adeline's
and DotEmu's invisible walls are 0.1.

-d prints each column the two files' grids or patches differ in, the
cells of a then those of b, up to the highest filled. -m prints the
tops of the columns x0 to x1 by z0 to z1, within 0 to 63, the height a
figure stands at on each column's highest filled cell, a row a z, and
. for an empty column. -l prints a scene's layouts (entry n of
LBA_BLL.HQR), each with its size dx dy dz, whether the grid's bits name
it, and its bricks, each its collision code, its material and its
sprite of LBA_BRK.HQR (0 none).

-t writes sources/gridfix.c on its standard output: the cells the fixes
of the grids write as CopyMapToCube builds the cube. dos.hqr is the
LBA_GRI.HQR of DotEmu's DOS copy of the North American CD, the same
bytes in GOG's install (Speedrun\\Windows) and in Steam's
(DotEmu\\original): the North American grids with the European floor
brick of grid 5 and 97 columns of the PlayStation version's cells, 19 in
grid 5, 2 in grid 8, 72 in grid 43 and 4 in patch 4. The table gives
first the PlayStation's cells of grids 5, 8 and 43, each column where
dos.hqr differs from the European CD's grid (eu.hqr), scene 5's four
armchairs aside, then the European CD's cells over the North American
CD's (us.hqr) in the grids they differ in, the PlayStation's parapet
over scene 109's ledge between them, five columns of the tool's own
table, taken from the PlayStation's disc, which alone holds them, each
checked against the European grid. A row for each column: the grid, x,
z, the height of the first cell apart and the count of cells to the
last, then the cells the grid holds there and the fix's. Then the
layouts those cells bring to a grid whose last 32 bytes leave them
clear, each a grid and a layout. -c compares that text with file and
fails if they differ.
"""
import os
import struct
import sys

SIDE = 64
HIGH = 25
PATCHES = 120


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
    """The file's entries expanded, None if empty."""
    with open(path, "rb") as f:
        b = f.read()
    out = []
    for i in range(struct.unpack_from("<I", b, 0)[0] // 4):
        off = struct.unpack_from("<I", b, 4 * i)[0]
        if off == 0 or off + 10 > len(b):
            out.append(None)
            continue
        size, packed, method = struct.unpack_from("<IIH", b, off)
        data = b[off + 10:off + 10 + packed]
        out.append(data[:size] if method == 0 else expand(data, size))
    return out


def entry(path, n):
    e = entries(path)
    if not 0 <= n < len(e) or e[n] is None:
        fail("%s: no entry %d" % (path, n))
    return e[n]


def column(g, x, z):
    """Column x, z of grid g, its cells from y 0, as grille_a.c's
    DecompColonne unpacks them: a count of runs, each a byte, 00 and n
    for n+1 empty cells, 01 and n for the n+1 cells that follow, 10 and n
    for a cell n+1 times."""
    s = struct.unpack_from("<H", g, 2 * (x + SIDE * z))[0]
    runs = g[s] or 256
    s += 1
    cells = []
    for _ in range(runs):
        op = g[s]
        s += 1
        n = (op & 0x3F) + 1
        if op & 0xC0 == 0:
            cells += [(0, 0)] * n
        elif op & 0x40:
            cells += [(g[s + 2 * i], g[s + 2 * i + 1]) for i in range(n)]
            s += 2 * n
        else:
            cells += [(g[s], g[s + 1])] * n
            s += 2
    return (cells + [(0, 0)] * HIGH)[:HIGH]


def cells(c, top):
    return " ".join("%d.%d" % p if p != (0, 0) else "." for p in c[:top])


def diff(a, b, wanted):
    ea, eb = entries(a), entries(b)
    for n in wanted or range(min(len(ea), len(eb))):
        ga = ea[n] if n < len(ea) else None
        gb = eb[n] if n < len(eb) else None
        if ga is None or gb is None or ga == gb:
            continue
        what = "patch %d" % (n - PATCHES) if n >= PATCHES else "grid %d" % n
        for z in range(SIDE):
            for x in range(SIDE):
                ca, cb = column(ga, x, z), column(gb, x, z)
                if ca == cb:
                    continue
                top = max(y for y in range(HIGH) if ca[y] != (0, 0) or cb[y] != (0, 0)) + 1
                print("%s x %d z %d\n\t%s\n\t%s" % (what, x, z, cells(ca, top), cells(cb, top)))


def heights(path, n, x0, x1, z0, z1):
    g = entry(path, n)
    print("    " + "".join("%4d" % x for x in range(x0, x1 + 1)))
    for z in range(z0, z1 + 1):
        row = []
        for x in range(x0, x1 + 1):
            c = column(g, x, z)
            filled = [y for y in range(HIGH) if c[y] != (0, 0)]
            row.append("%4d" % (max(filled) + 1) if filled else "   .")
        print("%3d " % z + "".join(row))


def layouts(bllpath, gripath, n, wanted):
    bll = entry(bllpath, n)
    named = used(entry(gripath, n))
    count = struct.unpack_from("<I", bll, 0)[0] // 4
    for i in wanted or range(1, count + 1):
        if not 1 <= i <= count:
            fail("%s: entry %d has no layout %d" % (bllpath, n, i))
        off = struct.unpack_from("<I", bll, 4 * (i - 1))[0]
        dx, dy, dz = bll[off:off + 3]
        bricks = []
        for j in range(dx * dy * dz):
            p = off + 3 + 4 * j
            bricks.append("%d/%d/%d" % (bll[p], bll[p + 1], struct.unpack_from("<H", bll, p + 2)[0]))
        print("layout %d %dx%dx%d %s %s" % (i, dx, dy, dz, "used" if i in named else "unused", " ".join(bricks)))


# The grids where dos.hqr's columns of the PlayStation version's cells
# fix a fault, each with what its cells do.
PSFIX = (
    (5, "Scene 5, the hall: the east plants and the chest capped, the low walls raised."),
    (8, "Scene 8: the border rocks at x 16, z 62 and 63 raised to 16."),
    (43, "Scene 43, the museum: nine barrels about the grate, the balustrades raised."),
)

# Scene 5's armchairs, x and z, which the PlayStation caps too, to no
# fault.
ARMCHAIRS = {(22, 24), (23, 24), (26, 20), (26, 21)}

# The PlayStation's parapet on scene 109's ledge, which its disc alone
# holds: the five columns its grid holds apart from the European grid,
# taken from the disc's SCENES.BLK, its layouts given the PC's numbers,
# a row each: x, z, the height of the first cell apart, the cells the
# European grid holds from there and the PlayStation's.
PARAPET = (
    (41, 24, 3, [(0, 0)], [(12, 2)]),
    (42, 24, 2, [(0, 0), (0, 0)], [(0, 1), (12, 3)]),
    (41, 25, 1, [(0, 0), (0, 1), (2, 14), (2, 17), (0, 0)],
     [(0, 1), (0, 1), (89, 2), (89, 3), (83, 0)]),
    (42, 25, 1, [(0, 0), (0, 1), (2, 13), (2, 16), (0, 0)],
     [(0, 1), (0, 1), (89, 2), (89, 3), (83, 0)]),
    (43, 25, 1, [(0, 0), (0, 1), (2, 14), (2, 17), (0, 0)],
     [(0, 1), (0, 1), (89, 2), (89, 3), (83, 0)]),
)


def fixrows(ga, gb, leave):
    """The rows that give grid ga grid gb's cells: for each column apart
    but those of leave, x, z, the height of its first cell apart, then
    ga's cells from there to its last apart and gb's."""
    rows = []
    for z in range(SIDE):
        for x in range(SIDE):
            if (x, z) in leave:
                continue
            ca, cb = column(ga, x, z), column(gb, x, z)
            ys = [y for y in range(HIGH) if ca[y] != cb[y]]
            if ys:
                rows.append((x, z, ys[0], ca[ys[0]:ys[-1] + 1], cb[ys[0]:ys[-1] + 1]))
    return rows


def comment(text, indent):
    """A block comment holding text, its lines filled to 72 columns."""
    lines = [indent + "/*"]
    line = indent + " *"
    for word in text.split():
        if len((line + " " + word).expandtabs()) > 72:
            lines.append(line)
            line = indent + " *"
        line += " " + word
    return lines + [line, indent + " */"]


def used(g):
    """The layouts grid g's last 32 bytes name, a bit a layout from the
    high bit of the first byte, as LoadUsedBrick reads them."""
    b = g[-32:]
    return {i for i in range(256) if b[i >> 3] >> (7 - (i & 7)) & 1}


def parapet(g, brought):
    """The table's rows of PARAPET, each checked against g, the European
    grid 109, the layouts its cells bring to the grid added to
    brought."""
    lines = comment(
        "Scene 109: a parapet over the ledge, the PlayStation's cells, "
        "which its disc alone holds.", "\t")
    for x, z, y, a, b in PARAPET:
        if list(column(g, x, z)[y:y + len(a)]) != a:
            fail("grid 109: %d, %d: other cells than the parapet's" % (x, z))
        lines.append("\t%d, %d, %d, %d, %d," % (109, x, z, y, len(a)))
        for cs in (a, b):
            lines.append("\t\t" + " ".join("%d, %d," % c for c in cs))
        brought |= {(109, l) for l, _ in b if l != 0 and l not in used(g)}
    return lines


def gridfix(eu, us, dos):
    """sources/gridfix.c's text."""
    lines = ["/* Made by tools/grids.py -t, and held to it by -c. Do not edit. */", ""]
    lines += comment(
        "The cells the fixes of the grids write as CopyMapToCube builds the "
        "cube, a row for each column: the grid, its x and z, the height y "
        "of the first cell the fix changes and the count n of cells from "
        "there, the n cells the grid holds there, then the n cells the fix "
        "writes in their place, each a layout and a brick, written where the "
        "column holds the row's first n, so that a grid fixed already keeps "
        "its own. 255 ends the table.", "")
    lines.append("unsigned char gridfixes[] = {")
    lines += comment(
        "The PlayStation version's cells (SLES_006.98), as the LBA_GRI.HQR "
        "of DotEmu's DOS copy of the North American CD holds them, in GOG's "
        "Speedrun\\Windows and Steam's DotEmu\\original: its columns of "
        "grids 5, 8 and 43 apart from the European grids, scene 5's four "
        "armchairs left out.", "\t")
    brought = set()
    parts = [(n, what, eu[n], dos[n], ARMCHAIRS if n == 5 else ()) for n, what in PSFIX]
    parts += [(n, None, us[n], eu[n], ()) for n in range(PATCHES)
              if us[n] is not None and eu[n] is not None and us[n] != eu[n]]
    for n, what, ga, gb, leave in parts:
        if what is not None:
            lines += comment(what, "\t")
        elif n == parts[len(PSFIX)][0]:
            lines += parapet(eu[109], brought)
            lines += comment(
                "Adeline's cells of the European CD (1994-11-14) in place "
                "of the North American CD's (1994-10-10), which DotEmu's DOS "
                "copy holds too in 16 of these 17 columns, its grid 5 holding "
                "the European floor brick.", "\t")
        for x, z, y, a, b in fixrows(ga, gb, leave):
            lines.append("\t%d, %d, %d, %d, %d," % (n, x, z, y, len(a)))
            for cs in (a, b):
                lines.append("\t\t" + " ".join("%d, %d," % c for c in cs))
            brought |= {(n, l) for l, _ in b if l != 0 and l not in used(ga)}
    lines += ["\t255,", "};", ""]
    lines += comment(
        "The layouts the cells above bring to a grid whose last 32 bytes "
        "leave them clear, a grid and a layout a row, which InitGrille names "
        "before LoadUsedBrick loads the bricks. 255 ends the table.", "")
    lines.append("unsigned char gridlayouts[] = {")
    lines += ["\t%d, %d," % nl for nl in sorted(brought)]
    lines += ["\t255,", "};"]
    return "\n".join(lines) + "\n"


def check(path, text):
    with open(path) as f:
        if f.read() != text:
            fail("%s differs" % path)


def main():
    a = sys.argv[1:]
    try:
        if len(a) >= 3 and a[0] == "-d":
            diff(a[1], a[2], [int(v) for v in a[3:]])
        elif len(a) == 7 and a[0] == "-m":
            n, x0, x1, z0, z1 = [int(v) for v in a[2:]]
            if not (0 <= x0 <= x1 < SIDE and 0 <= z0 <= z1 < SIDE):
                sys.stderr.write("grids.py: -m: x0 to x1 and z0 to z1 within 0 to %d\n" % (SIDE - 1))
                sys.exit(2)
            heights(a[1], n, x0, x1, z0, z1)
        elif len(a) >= 4 and a[0] == "-l":
            layouts(a[1], a[2], int(a[3]), [int(v) for v in a[4:]])
        elif len(a) == 4 and a[0] == "-t":
            sys.stdout.write(gridfix(entries(a[1]), entries(a[2]), entries(a[3])))
        elif len(a) == 5 and a[0] == "-c":
            check(a[1], gridfix(entries(a[2]), entries(a[3]), entries(a[4])))
        else:
            sys.stderr.write(__doc__)
            sys.exit(2)
    except ValueError:
        sys.stderr.write(__doc__)
        sys.exit(2)
    except OSError as e:
        sys.stderr.write("grids.py: %s\n" % e)
        sys.exit(1)


main()
