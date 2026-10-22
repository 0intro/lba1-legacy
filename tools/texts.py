"""texts.py: the fixes of TEXT.HQR's texts, written as InitDial loads a
bank: the European CD's texts over the North American CD's and over
DotEmu's, which cut the keys' names from the help, and the
PlayStation's corrections.

    texts.py -t us.hqr eu.hqr dotemu.hqr ps.hqr
    texts.py -c file us.hqr eu.hqr dotemu.hqr ps.hqr

TEXT.HQR holds two entries a bank, 14 banks a language (English,
French, German, Spanish, Italian): the texts' numbers, a word each,
then their offsets, a word each and one more for the end, and their
bytes in CP437, each text ending with a NUL. The files are the North
American CD's (us.hqr), the European CD's (eu.hqr), DotEmu's, which the
Classic and the Enhanced Edition ship (dotemu.hqr), and the
PlayStation version's (ps.hqr), whose entries its own LZS packs, its
copies 3 bytes longer, and whose font writes four characters by codes
of its own: 0x17 for ì, 0x18 í, 0x19 ° and 0x1a ß.

The definitive text of each language, bank and number is the European
CD's, or the PlayStation's where TAKE names it, its font's codes made
CP437 again: 13 corrections of typos, punctuation and the clovers'
count of a hint. Where OWN names it, the definitive is the European
with the PlayStation's corrections of errors alone, the tree's own
text: seven texts whose PlayStation's form changes more. The North
American CD's two texts more in Spanish and in Italian, strays of
bank 8 that the European CD drops, stay as they are.

-t writes sources/textfix.c on its standard output: a row in textfixes
for each text that one of the three PC files holds in another form than
the definitive, the language, the bank, the number (a byte, then a byte
of 256), the count n of those forms and their CRC-32s, each over the
form's bytes and its NUL, four bytes from the lowest, then in
fixedtexts the definitive text of each row, in the rows' order, a
PlayStation's correction and a text of the tree's own marked so in its
comment. -c compares that text with file and fails if they differ, and
proves the table: each text TAKE names is the PlayStation's correction,
each of OWN's corrections finds its old form once in the European text
and its new one in the PlayStation's, each PC file's banks, their texts
replaced where a row knows their form, hold the definitive texts, and
each such bank fits the 1024 bytes of BufOrder and the 25000
of BufText.
"""
import os
import struct
import sys
import zlib

LANGS = ("EN", "FR", "DE", "SP", "IT")
BANKS = 14
ORDER = 1024
TEXT = 25000

# The PlayStation's font's codes for the characters it lacks, and their
# CP437 bytes.
PSFONT = {0x17: 0x8D, 0x18: 0xA1, 0x19: 0xF8, 0x1A: 0xE1}

# The PlayStation's corrections taken whole, each a language, a bank
# and a number: each of their changes fixes a typo or the punctuation
# that the texts' 1994 spelling asks, and in French 2.162, the list of
# places, the clovers that the sandbags' soldier and the ruins' clones
# guard are counted as the scenes hold them (scene 3's actor 10, a
# clover worth 3, scene 17's actors 1, 5 and 6, one each), its "boîte
# de trèfle" made "boîte à trèfle".
TAKE = (
    ("FR", 2, 162),
    ("FR", 4, 70), ("FR", 4, 105), ("FR", 6, 45), ("FR", 8, 38),
    ("FR", 12, 6), ("FR", 12, 7),
    ("DE", 2, 109), ("DE", 4, 18), ("DE", 6, 47), ("DE", 6, 80),
    ("DE", 12, 1),
    ("SP", 12, 6),
)

# The PlayStation's corrections of errors in texts whose form there
# changes more, each made in the European text: a language, a bank and
# a number, then each correction, the European words and the
# PlayStation's. Its other changes in these texts are its own choices:
# spellings made British (EN 2.150), sentences joined by a comma split
# (DE 2.110, 2.161, 5.11, 5.55, 8.22), the comma before a main clause's
# "und" dropped and "Nord-Halbkugel" written whole (DE 7.1), and the
# Klares Wasser, a name, written in lower case (DE 8.22).
OWN = {
    ("EN", 2, 150): (("It's rotational", "Its rotational"),),
    ("DE", 2, 110): (("auszuweisen", "ausweisen"),),
    ("DE", 2, 161): (
        ("kommen an dem", "kommen, an dem"),
        ('die @ " Schlüssel der Macht gehören mir ! @',
         'die @ Schlüssel der Macht gehören mir !" @'),
        ("seint,", "sein,"),
    ),
    ("DE", 5, 11): (("Höle", "Höhle"),),
    ("DE", 5, 55): (
        ("Auserwählte von", "Auserwählte, von"),
        ("Brunnen um", "Brunnen, um"),
        ("hinterlassen um", "hinterlassen, um"),
    ),
    ("DE", 7, 1): (("können was", "können, was"),),
    ("DE", 8, 22): (("Spiels Du", "Spielst Du"),),
}

# The texts the PlayStation changes that the tree keeps as the European
# CD has them, each change its own choice: in German the Rebellion-Insel
# renamed (2.204, 3.141, 4.119, 5.18, 6.63, 8.10, 8.11, 8.31), sentences
# joined by a comma split (4.37, 7.15, 10.10), the Klares Wasser, a
# name, written in lower case and a sentence split (8.23), "der" made
# "Der" after a comma (4.11), "daran" made "darin" before "etwas zu
# ändern" (4.110), two colons made a period and an exclamation mark
# before a word in lower case (10.9) and "auch" dropped (7.3), in Spanish
# "Capitán" dropped from the pirate's name (4.143), a space and byte 1
# added at three ends (6.188, 10.6, 10.60), in French a sentence on the
# Clear Water added to two texts (2.107, 2.157), and in Italian the keys
# made the pad's buttons (2.112), beside the texts of its interface.


def fail(msg):
    sys.stderr.write("%s: %s\n" % (os.path.basename(sys.argv[0]), msg))
    sys.exit(1)


def expand(src, size, add):
    """LZS's bytes expanded to size, each copy add bytes longer than its
    count: 2 the PC's, 3 the PlayStation's."""
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
                for k in range((w & 0xF) + add):
                    out.append(out[start + k])
            flags >>= 1
    return bytes(out[:size])


def entries(path):
    """The file's entries expanded, None for an empty one."""
    with open(path, "rb") as f:
        b = f.read()
    out = []
    for k in range(struct.unpack_from("<I", b, 0)[0] // 4):
        off = struct.unpack_from("<I", b, 4 * k)[0]
        if off == 0 or off + 10 > len(b):
            out.append(None)
            continue
        size, packed, method = struct.unpack_from("<IIH", b, off)
        data = b[off + 10:off + 10 + packed]
        if method == 0:
            out.append(data[:size])
        elif method in (1, 2):
            out.append(expand(data, size, method + 1))
        else:
            fail("%s: entry %d: method %d" % (path, k, method))
    return out


def banks(path):
    """The file's banks, each its language and number and its texts,
    a list of numbers and bytes, each text's NUL kept."""
    es = entries(path)
    out = {}
    for lang in range(len(LANGS)):
        for bank in range(BANKS):
            k = 2 * (lang * BANKS + bank)
            order, text = es[k], es[k + 1]
            nums = [struct.unpack_from("<H", order, 2 * i)[0] for i in range(len(order) // 2)]
            offs = [struct.unpack_from("<H", text, 2 * i)[0] for i in range(len(nums) + 1)]
            out[(lang, bank)] = [(n, text[offs[i]:offs[i + 1]]) for i, n in enumerate(nums)]
    return out


def texts(bs):
    """Each text of banks bs by its language, bank and number."""
    return {(lb[0], lb[1], n): t for lb, ts in bs.items() for n, t in ts}


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def definitive(eu, ps):
    """The definitive texts: the European, with the PlayStation's
    corrections TAKE names and OWN's made in the texts it names."""
    d = dict(eu)
    for lang, bank, num in TAKE:
        k = (LANGS.index(lang), bank, num)
        d[k] = bytes(PSFONT.get(c, c) for c in ps[k])
    for (lang, bank, num), cs in OWN.items():
        k = (LANGS.index(lang), bank, num)
        for old, new in cs:
            d[k] = d[k].replace(old.encode("cp437"), new.encode("cp437"))
    return d


def rows(files, d):
    """For each text that a file holds in another form than the
    definitive, its key, the CRC-32s of those forms and the definitive
    text."""
    out = []
    for k in sorted(d):
        forms = sorted({crc(f[k]) for f in files if k in f and f[k] != d[k]})
        if forms:
            out.append((k, forms, d[k]))
    return out


def literal(b, indent):
    """Bytes b, their NUL left off, as C string literals, a line each
    of at most 72 columns, broken after a blank."""
    lines = []
    cur = ""
    word = ""
    for c in b[:-1]:
        if c == 0x22 or c == 0x5C:
            e = "\\" + chr(c)
        elif 0x20 <= c < 0x7F:
            e = chr(c)
        else:
            e = "\\%03o" % c
        word += e
        if c == 0x20:
            if cur and len((indent + '"' + cur + word + '"').expandtabs()) > 72:
                lines.append(cur)
                cur = ""
            cur += word
            word = ""
    if word:
        if cur and len((indent + '"' + cur + word + '"').expandtabs()) > 72:
            lines.append(cur)
            cur = ""
        cur += word
    lines.append(cur)
    return [indent + '"' + l + '"' for l in lines]


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


def textfix(rs):
    """sources/textfix.c's text."""
    lines = ["/* Made by tools/texts.py -t, and held to it by -c. Do not edit. */", ""]
    lines += comment(
        "The texts the fixes of TEXT.HQR write as InitDial loads a bank, a "
        "row for each text that a file holds in another form than the "
        "definitive: the language, the bank, the text's number, a byte then "
        "a byte of 256, the count n of the forms the fix replaces, and their "
        "CRC-32s, each over the form's bytes and its NUL, four bytes from "
        "the lowest. 255 ends the table.", "")
    lines.append("unsigned char textfixes[] = {")
    for (lang, bank, num), forms, _ in rs:
        b = [lang, bank, num & 255, num >> 8, len(forms)]
        for f in forms:
            b += [f & 255, f >> 8 & 255, f >> 16 & 255, f >> 24]
        lines.append("\t" + " ".join("%d," % v for v in b))
    lines += ["\t255,", "};", ""]
    lines += comment(
        "The text each row of textfixes writes, in the rows' order: the "
        "European CD's, the PlayStation version's correction of it "
        "(SLES_006.98), or the European with the PlayStation's corrections "
        "of errors alone, the tree's own, marked so.", "")
    lines.append("char *fixedtexts[] = {")
    take = {(LANGS.index(l), b, n) for l, b, n in TAKE}
    own = {(LANGS.index(l), b, n) for l, b, n in OWN}
    for (lang, bank, num), _, t in rs:
        mark = ""
        if (lang, bank, num) in take:
            mark = ", the PlayStation's"
        elif (lang, bank, num) in own:
            mark = ", the tree's"
        lines.append("\t/* %s %d.%d%s */" % (LANGS[lang], bank, num, mark))
        ls = literal(t, "\t")
        ls[-1] += ","
        lines += ls
    lines += ["};"]
    return "\n".join(lines) + "\n"


def fix(bank, lb, rs):
    """Bank lb's texts with each replaced where a row knows its form, as
    InitDial's fix writes them."""
    out = []
    for n, t in bank:
        for (lang, b, num), forms, d in rs:
            if (lang, b, num) == (lb[0], lb[1], n) and crc(t) in forms:
                t = d
                break
        out.append((n, t))
    return out


def check(path, text, us, eu, dotemu, ps, rs, d):
    with open(path) as f:
        if f.read() != text:
            fail("%s differs" % path)
    pe, pp = texts(eu), texts(ps)
    for lang, bank, num in TAKE:
        k = (LANGS.index(lang), bank, num)
        if k not in pp or pp[k] == pe[k] or d[k] == pe[k]:
            fail("%s %d.%d: the PlayStation's text is the European" % (lang, bank, num))
    for (lang, bank, num), cs in OWN.items():
        k = (LANGS.index(lang), bank, num)
        p = bytes(PSFONT.get(c, c) for c in pp[k])
        for old, new in cs:
            if pe[k].count(old.encode("cp437")) != 1 or new.encode("cp437") not in p:
                fail("%s %d.%d: %s: no correction of the PlayStation's" % (lang, bank, num, old))
        if d[k] == pe[k]:
            fail("%s %d.%d: the tree's text is the European" % (lang, bank, num))
    if set(texts(dotemu)) != set(pe) or set(pp) != set(pe):
        fail("DotEmu's or the PlayStation's texts are other than the European")
    strays = set(texts(us)) - set(pe)
    if set(pe) - set(texts(us)) or len(strays) != 4 or {(k[0], k[1]) for k in strays} != {(3, 8), (4, 8)}:
        fail("the North American texts are other than the European and four strays")
    for name, bs in (("us", us), ("eu", eu), ("dotemu", dotemu)):
        for lb, bank in sorted(bs.items()):
            fixed = fix(bank, lb, rs)
            for n, t in fixed:
                k = (lb[0], lb[1], n)
                if k in d and t != d[k]:
                    fail("%s: %s %d.%d: the fix leaves another text" % (name, LANGS[k[0]], k[1], k[2]))
            if 2 * len(fixed) > ORDER or 2 * (len(fixed) + 1) + sum(len(t) for _, t in fixed) > TEXT:
                fail("%s: %s bank %d: past BufOrder or BufText" % (name, LANGS[lb[0]], lb[1]))


def usage():
    sys.stderr.write("usage: texts.py -t us.hqr eu.hqr dotemu.hqr ps.hqr\n"
                     "       texts.py -c file us.hqr eu.hqr dotemu.hqr ps.hqr\n")
    sys.exit(2)


def main():
    a = sys.argv[1:]
    if not ((len(a) == 5 and a[0] == "-t") or (len(a) == 6 and a[0] == "-c")):
        usage()
    try:
        us, eu, dotemu, ps = [banks(p) for p in a[-4:]]
        d = definitive(texts(eu), texts(ps))
        rs = rows([texts(us), texts(eu), texts(dotemu)], d)
        text = textfix(rs)
        if a[0] == "-t":
            sys.stdout.write(text)
        else:
            check(a[1], text, us, eu, dotemu, ps, rs, d)
    except OSError as e:
        fail(str(e))


main()
