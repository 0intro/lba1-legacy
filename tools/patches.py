"""patches.py: the places each scene's scripts rewrite as they run, as
the PlayStation's scenes list them, proved and mended, written as
sources/patch.c.

    patches.py [-g gamedir] [-p psdir] [-c file]

gamedir holds the PC's SCENE.HQR, psdir the PlayStation's GAME.HQR and
SCENES.BLK. They default to pc/LBA and ps in $LBATMP, the tree's
../lba1-tmp when LBATMP is unset. patches.py writes the C on its
standard output, or with -c compares it with file and fails if they
differ. It exits 1 when a proof fails, saying which.

The scripts rewrite their own bytes in seven places, as gerelife.c and
geretrak.c run them. A life script's SWIF and SNIF turn into each other
and its ONEIF into NEVERIF, at the opcode's byte. A track's WAIT_NB_ANIM
counts in the byte after its count, WAIT_NB_SECOND keeps its end time in
the 4 bytes after its seconds, FACE_TWINKEL its angle in the word after
the opcode and ANGLE_RND in the word after its range. SCENE.HQR holds
them as they are before the scene runs. A save of the running scene
keeps their bytes, which its load writes over the scene loaded afresh:
LBA2's SaveContexte, from the list its compiler makes of each scene's
(AddPatch), and the PlayStation's save.

The PlayStation's scene, part 8 of each scene of SCENES.BLK, ends with
that list, which its save (0x8002cbf8) keeps and its load (0x8002d5ec)
writes back: 4-aligned after the scripts, a count, then 8 bytes an
entry, the place's size (1, 2 or 4) and its offset in the scene as
halfwords, then the value it holds as loaded. Its scene is the PC's
laid out otherwise: the counts of actors, zones and points at 0x24,
0x26 and 0x28, from 0x2c a record of 0x2c bytes an actor (the sizes of
its track and life at 0x28 and 0x2a), 0x18 bytes a zone, 8 a point,
then each actor's track and life. The PC's (LoadScene) holds 45 bytes
of the scene and the hero's start, the hero's track and life, each its
size as a word then its bytes, the count of actors, then for each
other actor 35 bytes, its track and its life. The two hold the same
scripts but for the numbers of the samples (691 track instructions,
14 of them the count of a REPEAT_SAMPLE)
and of the brick overlays (nine SET_GRM, 0 on the PlayStation where
the PC's are 6 to 13, in scenes 36, 38, 87, 88, 92, 95 and 99).

walk finds a script's places by reading it from its start, an
instruction at a time, as the interpreters read it (DoLife, DoFuncLife,
DoTest, DoTrack). It must end at the script's end, meet no opcode,
function or test the interpreters lack, and find each place a jump, a
SET_LIFE or a SET_TRACK names at the start of an instruction.

The PlayStation's lists were made by such a walk with two departures.
It reads the value COL_OBJ is compared with as a word, where DoTest
reads a byte, and reads a condition's value only when its function and
its test are known, stepping over an unknown opcode as a byte. The first
puts it out of step after a COL_OBJ condition until an instruction it
reads starts where one does (the second shows out of step alone), so
that it lists bytes of operands that hold 2, 13 or 14 and misses places.
patches.py proves that a walk with both departures gives each of the
119 lists, entry for entry and in order, and each entry's value the
scene's bytes there. On the PC's scenes, the walk must find the places
it finds on the PlayStation's (mapped through the actor, its script and
the place there), and the PC's bytes must hold there each value the
PlayStation's list gives.

The C gives each scene's places as the walk finds them in the PC's
scene, in the scripts' order, a place's size then its offset in the
scene, then the index of each scene's first place, and the end, then
the scene's own list, which LoadScene sets, as LBA2's PATCH.CPP. They
are the PlayStation's list, but in the scenes where its walk went out of
step, which the C names, and scene 119, the credits, which the
PlayStation lacks.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.environ.get('LBATMP') or os.path.join(HERE, '..', '..', 'lba1-tmp')

# The life script's opcodes (common.h's LM_), each a name and its
# operands as DoLife reads them (gerelife.c): b a byte, w a word, j a
# word giving a place in the actor's own life, aj an actor's number and
# a place in its life, t a place in the actor's own track, at an actor's
# number and a place in its track, m a move and the actor followed when
# the move is MOVE_FOLLOW (2), bm an actor's number then that, s a
# string up to its 0, c a condition: DoFuncLife's function and its
# operand, DoTest's test and value, then the place it jumps to.
LIFE = {
    0: ('END', ''), 1: ('NOP', ''), 2: ('SNIF', 'c'), 3: ('OFFSET', 'j'),
    4: ('NEVERIF', 'c'), 10: ('LABEL', 'b'), 11: ('RETURN', ''), 12: ('IF', 'c'),
    13: ('SWIF', 'c'), 14: ('ONEIF', 'c'), 15: ('ELSE', 'j'), 16: ('ENDIF', ''),
    17: ('BODY', 'b'), 18: ('BODY_OBJ', 'bb'), 19: ('ANIM', 'b'), 20: ('ANIM_OBJ', 'bb'),
    21: ('SET_LIFE', 'j'), 22: ('SET_LIFE_OBJ', 'aj'), 23: ('SET_TRACK', 't'),
    24: ('SET_TRACK_OBJ', 'at'), 25: ('MESSAGE', 'w'), 26: ('FALLABLE', 'b'),
    27: ('SET_DIR', 'm'), 28: ('SET_DIR_OBJ', 'bm'), 29: ('CAM_FOLLOW', 'b'),
    30: ('COMPORTEMENT_HERO', 'b'), 31: ('SET_FLAG_CUBE', 'bb'), 32: ('COMPORTEMENT', 'b'),
    33: ('SET_COMPORTEMENT', 'j'), 34: ('SET_COMPORTEMENT_OBJ', 'aj'),
    35: ('END_COMPORTEMENT', ''), 36: ('SET_FLAG_GAME', 'bb'), 37: ('KILL_OBJ', 'b'),
    38: ('SUICIDE', ''), 39: ('USE_ONE_LITTLE_KEY', ''), 40: ('GIVE_GOLD_PIECES', 'w'),
    41: ('END_LIFE', ''), 42: ('STOP_L_TRACK', ''), 43: ('RESTORE_L_TRACK', ''),
    44: ('MESSAGE_OBJ', 'bw'), 45: ('INC_CHAPTER', ''), 46: ('FOUND_OBJECT', 'b'),
    47: ('SET_DOOR_LEFT', 'w'), 48: ('SET_DOOR_RIGHT', 'w'), 49: ('SET_DOOR_UP', 'w'),
    50: ('SET_DOOR_DOWN', 'w'), 51: ('GIVE_BONUS', 'b'), 52: ('CHANGE_CUBE', 'b'),
    53: ('OBJ_COL', 'b'), 54: ('BRICK_COL', 'b'), 55: ('OR_IF', 'c'), 56: ('INVISIBLE', 'b'),
    57: ('ZOOM', 'b'), 58: ('POS_POINT', 'b'), 59: ('SET_MAGIC_LEVEL', 'b'),
    60: ('SUB_MAGIC_POINT', 'b'), 61: ('SET_LIFE_POINT_OBJ', 'bb'),
    62: ('SUB_LIFE_POINT_OBJ', 'bb'), 63: ('HIT_OBJ', 'bb'), 64: ('PLAY_FLA', 's'),
    65: ('PLAY_MIDI', 'b'), 66: ('INC_CLOVER_BOX', ''), 67: ('SET_USED_INVENTORY', 'b'),
    68: ('ADD_CHOICE', 'w'), 69: ('ASK_CHOICE', 'w'), 70: ('BIG_MESSAGE', 'w'),
    71: ('INIT_PINGOUIN', 'b'), 72: ('SET_HOLO_POS', 'b'), 73: ('CLR_HOLO_POS', 'b'),
    74: ('ADD_FUEL', 'b'), 75: ('SUB_FUEL', 'b'), 76: ('SET_GRM', 'b'),
    77: ('SAY_MESSAGE', 'w'), 78: ('SAY_MESSAGE_OBJ', 'bw'), 79: ('FULL_POINT', ''),
    80: ('BETA', 'w'), 81: ('GRM_OFF', ''), 82: ('FADE_PAL_RED', ''),
    83: ('FADE_ALARM_RED', ''), 84: ('FADE_ALARM_PAL', ''), 85: ('FADE_RED_PAL', ''),
    86: ('FADE_RED_ALARM', ''), 87: ('FADE_PAL_ALARM', ''), 88: ('EXPLODE_OBJ', 'b'),
    89: ('BULLE_ON', ''), 90: ('BULLE_OFF', ''), 91: ('ASK_CHOICE_OBJ', 'bw'),
    92: ('SET_DARK_PAL', ''), 93: ('SET_NORMAL_PAL', ''), 94: ('MESSAGE_SENDELL', ''),
    95: ('ANIM_SET', 'b'), 96: ('HOLOMAP_TRAJ', 'b'), 97: ('GAME_OVER', ''),
    98: ('THE_END', ''), 99: ('MIDI_OFF', ''), 100: ('PLAY_CD_TRACK', 'b'),
    101: ('PROJ_ISO', ''), 102: ('PROJ_3D', ''), 103: ('TEXT', 'w'), 104: ('CLEAR_TEXT', ''),
    105: ('BRUTAL_EXIT', ''),
}

# DoFuncLife's functions (common.h's LF_, 0 to 29) that read an operand
# byte: COL_OBJ, DISTANCE, ZONE_OBJ, BODY_OBJ, ANIM_OBJ, L_TRACK_OBJ,
# FLAG_CUBE, CONE_VIEW, FLAG_GAME, LIFE_POINT_OBJ, DISTANCE_3D,
# USE_INVENTORY. Those whose answer is a word, which DoTest compares
# with a word: DISTANCE, CONE_VIEW, NB_GOLD_PIECES, DISTANCE_3D, CHOICE.
# The others answer a byte. DoTest's tests (LT_) run from 0 to 5.
FUNCOPERAND = {1, 2, 4, 6, 8, 10, 11, 12, 15, 17, 22, 25}
FUNCWORD = {2, 12, 19, 22, 26}
NFUNCS = 30
NTESTS = 6
COLOBJ = 1

# The track's opcodes (common.h's TM_), as DoTrack reads them
# (geretrak.c): l is 4 bytes, the rest as for LIFE.
TRACK = {
    0: ('END', ''), 1: ('NOP', ''), 2: ('BODY', 'b'), 3: ('ANIM', 'b'),
    4: ('GOTO_POINT', 'b'), 5: ('WAIT_ANIM', ''), 6: ('LOOP', ''), 7: ('ANGLE', 'w'),
    8: ('POS_POINT', 'b'), 9: ('LABEL', 'b'), 10: ('GOTO', 't'), 11: ('STOP', ''),
    12: ('GOTO_SYM_POINT', 'b'), 13: ('WAIT_NB_ANIM', 'bb'), 14: ('SAMPLE', 'w'),
    15: ('GOTO_POINT_3D', 'b'), 16: ('SPEED', 'w'), 17: ('BACKGROUND', 'b'),
    18: ('WAIT_NB_SECOND', 'bl'), 19: ('NO_BODY', ''), 20: ('BETA', 'w'),
    21: ('OPEN_LEFT', 'w'), 22: ('OPEN_RIGHT', 'w'), 23: ('OPEN_UP', 'w'),
    24: ('OPEN_DOWN', 'w'), 25: ('CLOSE', ''), 26: ('WAIT_DOOR', ''),
    27: ('SAMPLE_RND', 'w'), 28: ('SAMPLE_ALWAYS', 'w'), 29: ('SAMPLE_STOP', 'w'),
    30: ('PLAY_FLA', 's'), 31: ('REPEAT_SAMPLE', 'w'), 32: ('SIMPLE_SAMPLE', 'w'),
    33: ('FACE_TWINKEL', 'w'), 34: ('ANGLE_RND', 'ww'),
}

# The places an instruction rewrites: its script and opcode, then the
# place's distance from the opcode and its size.
REWRITES = {
    ('life', 2): (0, 1), ('life', 13): (0, 1), ('life', 14): (0, 1),
    ('track', 13): (2, 1), ('track', 18): (2, 4), ('track', 33): (1, 2),
    ('track', 34): (3, 2),
}

SIZES = {'b': 1, 'w': 2, 'l': 4, 'j': 2, 't': 2}

# The life's jumps (OFFSET, ELSE), whose place is an instruction, where
# the track's GOTO, SET_LIFE, SET_TRACK and their kin may also name none,
# -1, which ends the script.
JUMPS = {3, 15}
NONE = 0xFFFF


class Fail(Exception):
    pass


def u16(b, p):
    return struct.unpack_from('<H', b, p)[0]


def u32(b, p):
    return struct.unpack_from('<I', b, p)[0]


def expand(src, size, add):
    """LZS as the PC (add 2) and the PlayStation (add 3) pack it: a byte
    of flags for eight items, a set flag a byte as it is, a clear one a
    word, (w & 0xF) + add bytes from (w >> 4) + 1 back."""
    d = bytearray()
    s = 0
    while len(d) < size:
        flags = src[s]
        s += 1
        for _ in range(8):
            if len(d) >= size:
                break
            if flags & 1:
                d.append(src[s])
                s += 1
            else:
                w = src[s] | src[s + 1] << 8
                s += 2
                for _ in range((w & 0xF) + add):
                    d.append(d[-((w >> 4) + 1)])
            flags >>= 1
    return bytes(d[:size])


def hqr(path):
    """An HQR's entries, None for an empty one: method 0 stored, 1 the
    PC's LZS, 2 the PlayStation's."""
    with open(path, 'rb') as f:
        d = f.read()
    out = []
    for k in range(u32(d, 0) // 4):
        off = u32(d, 4 * k)
        if off == 0 or off + 10 > len(d):
            out.append(None)
            continue
        size, packed, method = struct.unpack_from('<IIH', d, off)
        raw = d[off + 10:off + 10 + packed]
        out.append(raw[:size] if method == 0 else expand(raw, size, 2 if method == 1 else 3))
    return out


def psscenes(psdir):
    """Each PlayStation scene's part 8, placed by the table of GAME.HQR's entry 10."""
    t = hqr(os.path.join(psdir, 'GAME.HQR'))[10]
    parts = []
    at = 0
    for i in range(len(t) // 8):
        size, stored = struct.unpack_from('<II', t, 8 * i)
        if i % 9 in (0, 3):
            at = (at + 0x7ff) & ~0x7ff
        parts.append((at, size, stored))
        at += stored
    out = []
    with open(os.path.join(psdir, 'SCENES.BLK'), 'rb') as f:
        for sc in range(len(parts) // 9):
            at, size, stored = parts[9 * sc + 8]
            f.seek(at)
            raw = f.read(stored)
            out.append(expand(raw, size, 3) if stored < size else raw[:size])
    return out


def pcscripts(s):
    """A PC scene's scripts: for each actor, its track's offset and
    bytes, then its life's."""
    p = 45
    out = []
    n = 1
    while len(out) < n:
        if out:
            p += 35
        scripts = []
        for _ in range(2):
            scripts.append((p + 2, s[p + 2:p + 2 + u16(s, p)]))
            p += 2 + u16(s, p)
        out.append(tuple(scripts))
        if len(out) == 1:
            n = u16(s, p)
            p += 2
    return out


def psscripts(scx):
    """A PlayStation scene's scripts as pcscripts gives the PC's, then
    its list: each entry's size, offset and value."""
    nobj, nzone, ntrack = struct.unpack_from('<hhh', scx, 0x24)
    p = 0x2c + nobj * 0x2c + nzone * 0x18 + ntrack * 8
    out = []
    for k in range(nobj):
        ts, ls = struct.unpack_from('<HH', scx, 0x2c + k * 0x2c + 0x28)
        out.append(((p, scx[p:p + ts]), (p + ts, scx[p + ts:p + ts + ls])))
        p += ts + ls
    p = (p + 3) & ~3
    count = u32(scx, p)
    ents = [struct.unpack_from('<HHi', scx, p + 4 + 8 * i) for i in range(count)]
    return out, ents


def walk(kind, prg, ps=False):
    """A script's instructions in turn: each its offset, opcode, the
    places it names (kind, actor or None for its own, offset, and whether
    it may name none) and the places it rewrites (offset, size). With ps,
    the PlayStation's walk, which reads on whatever it meets."""
    table = LIFE if kind == 'life' else TRACK
    p = 0
    out = []
    while p < len(prg):
        at = p
        op = prg[p]
        p += 1
        if op not in table and not ps:
            raise Fail('%s at %d: opcode %d' % (kind, at, op))
        layout = table.get(op, ('', ''))[1]
        targets = []
        actor = None
        for c in layout:
            if c == 'a':
                actor = prg[p]
                p += 1
            elif c == 'c':
                fn = prg[p] if p < len(prg) else 0
                p += 1 + (fn in FUNCOPERAND)
                test = prg[p] if p < len(prg) else 0
                p += 1
                if not ps and (fn >= NFUNCS or test >= NTESTS):
                    raise Fail('%s at %d: function %d, test %d' % (kind, at, fn, test))
                if not ps or (fn < NFUNCS and test < NTESTS):
                    p += 2 if fn in FUNCWORD or (ps and fn == COLOBJ) else 1
                if not ps:
                    targets.append(('life', None, u16(prg, p), False))
                p += 2
            elif c in 'jt':
                none = kind == 'track' or op not in JUMPS
                if not ps:
                    targets.append(('life' if c == 'j' else 'track', actor, u16(prg, p), none))
                p += 2
            elif c == 'm':
                p += 1 + (p < len(prg) and prg[p] == 2)
            elif c == 's':
                p = prg.index(0, p) + 1 if 0 in prg[p:] else len(prg) + 1
            else:
                p += SIZES[c]
        if p > len(prg) and not ps:
            raise Fail('%s at %d: %s runs past the end' % (kind, at, table[op][0]))
        rewrites = []
        if (kind, op) in REWRITES:
            d, size = REWRITES[(kind, op)]
            rewrites.append((at + d, size))
        out.append((at, op, targets, rewrites))
    return out


def scene(scripts, what):
    """The places a scene's scripts rewrite, each (actor, kind, offset in
    its script, size), every place they name checked."""
    walks = [{'track': walk('track', t), 'life': walk('life', life)}
             for (_, t), (_, life) in scripts]
    starts = [{k: {i[0] for i in w[k]} for k in w} for w in walks]
    places = []
    for n, w in enumerate(walks):
        for kind in ('track', 'life'):
            for at, op, targets, rewrites in w[kind]:
                for tk, who, off, none in targets:
                    actor = n if who is None else who
                    if none and off == NONE:
                        continue
                    if actor >= len(walks) or off not in starts[actor][tk]:
                        name = (LIFE if kind == 'life' else TRACK)[op][0]
                        raise Fail('%s actor %d %s at %d: %s names %s %d of actor %d' % (
                            what, n, kind, at, name, tk, off, actor))
                for off, size in rewrites:
                    places.append((n, kind, off, size))
    return places


def absolute(scripts, places):
    """Places as (offset in the scene, size)."""
    base = [{'track': t0, 'life': l0} for (t0, _), (l0, _) in scripts]
    return [(base[n][kind] + off, size) for n, kind, off, size in places]


def same(have, value, size):
    """Whether the bytes read signed match an entry's value, a byte's
    alone in its low 8 bits."""
    return have == value or (size == 1 and have & 0xFF == value & 0xFF)


def prove(pcs, scx, sc):
    """Scene sc's places in the PC's scene pcs, proved against the
    PlayStation's scene scx, then the counts of the PlayStation's
    entries out of step and of the places it missed."""
    what = 'scene %d' % sc
    ps, ents = psscripts(scx)
    pc = pcscripts(pcs)
    if len(ps) != len(pc):
        raise Fail('%s: %d actors, the PC %d' % (what, len(ps), len(pc)))
    for n in range(len(ps)):
        for k in range(2):
            if len(ps[n][k][1]) != len(pc[n][k][1]):
                raise Fail('%s actor %d: a script of %d bytes, the PC\'s %d' % (
                    what, n, len(ps[n][k][1]), len(pc[n][k][1])))
    made = []
    for (t0, t), (l0, life) in ps:
        for kind, p0, b in (('track', t0, t), ('life', l0, life)):
            made += [(p0 + off, size) for _, _, _, rw in walk(kind, b, True) for off, size in rw]
    if made != [(off, size) for size, off, _ in ents]:
        raise Fail('%s: the PlayStation\'s walk gives another list' % what)
    for size, off, value in ents:
        have = int.from_bytes(scx[off:off + size], 'little', signed=True)
        if not same(have, value, size):
            raise Fail('%s: the list gives %d at %d, the scene holds %d' % (what, value, off, have))
    places = scene(pc, what)
    if scene(ps, what + ' (PlayStation)') != places:
        raise Fail('%s: the PlayStation\'s scripts rewrite other places' % what)
    values = {(off, size): value for size, off, value in ents}
    psplaces = absolute(ps, places)
    for (off, size), (at, _) in zip(psplaces, absolute(pc, places)):
        if (off, size) in values:
            have = int.from_bytes(pcs[at:at + size], 'little', signed=True)
            if not same(have, values[(off, size)], size):
                raise Fail('%s: the list gives %d at %d, the PC holds %d at %d' % (
                    what, values[(off, size)], off, have, at))
    return absolute(pc, places), len(set(values) - set(psplaces)), len(set(psplaces) - set(values))


def table(game, psdir):
    """Each scene's places, the scenes whose PlayStation list is mended
    (each with its entries out of step and its places missed), and the
    count of the PlayStation's scenes."""
    pcs = [s for s in hqr(os.path.join(game, 'SCENE.HQR')) if s is not None]
    ps = psscenes(psdir)
    lists = []
    mended = []
    for sc, s in enumerate(pcs):
        if sc < len(ps):
            places, out, missed = prove(s, ps[sc], sc)
            if out or missed:
                mended.append((sc, out, missed))
        else:
            places = absolute(pcscripts(s), scene(pcscripts(s), 'scene %d' % sc))
        lists.append(places)
    return lists, mended, len(ps)


def wrap(items, lines):
    line = '	'
    for item in items:
        if len(line.expandtabs()) + len(item) > 72:
            lines.append(line.rstrip())
            line = '	'
        line += item + ' '
    lines.append(line.rstrip())


def comment(paragraphs):
    """A block comment holding paragraphs, its lines filled to 72
    columns."""
    lines = ['/*']
    for i, text in enumerate(paragraphs):
        if i > 0:
            lines.append(' *')
        line = ' *'
        for word in text.split():
            if len(line) + 1 + len(word) > 72:
                lines.append(line)
                line = ' *'
            line += ' ' + word
        lines.append(line)
    return lines + [' */']


def c(lists, mended, nps):
    """sources/patch.c's text."""
    scenes = [str(sc) for sc, _, _ in mended]
    out = sum(n for _, n, _ in mended)
    missed = sum(n for _, _, n in mended)
    lines = ['#include <stdint.h>', '#include "../sys/compat.h"', '#include "c_extern.h"', '',
             '/* Made by tools/patches.py, which proves them. Do not edit. */', '']
    lines += comment([
        "TabPatches is the lists of the places each scene's scripts rewrite as "
        "they run, as the PlayStation's SCENES.BLK holds them: a place's size, "
        "then its offset in SCENE.HQR's scene, scene by scene in the scripts' "
        "order. "
        "Scene %d, the credits, which the PlayStation lacks, has its places "
        "as the scripts rewrite them." % nps,
        "FIXED: the PlayStation's lists were made by a walk of the scripts "
        "that read the value COL_OBJ is compared with as a word, which went "
        "out of step in %d scenes, %s and %s. There they hold %d bytes of "
        "operands and miss %d places, whose state a load lost. Their lists "
        "here are the places their scripts rewrite."
        % (len(mended), ', '.join(scenes[:-1]), scenes[-1], out, missed)])
    lines.append('T_PATCH	TabPatches[] = {')
    for sc, places in enumerate(lists):
        lines.append('	/* %d */' % sc)
        wrap(['{%d, %d},' % (size, off) for off, size in places], lines)
    lines += ['};', '',
              "/* IndexPatches is each scene's first place in TabPatches, then the end */",
              'UWORD	IndexPatches[] = {']
    k = 0
    index = []
    for places in lists + [[]]:
        index.append('%d,' % k)
        k += len(places)
    wrap(index, lines)
    lines += ['};', '',
              '/* the scenes IndexPatches indexes */',
              'LONG	NbIndexPatches = sizeof IndexPatches / sizeof IndexPatches[0] - 1;', '',
              "/* the scene's own places in TabPatches and their count, which LoadScene sets */",
              'T_PATCH	*ListPatches;',
              'LONG	NbPatches;']
    return '\n'.join(lines) + '\n'


def usage():
    print('usage: patches.py [-g gamedir] [-p psdir] [-c file]', file=sys.stderr)
    sys.exit(2)


def main(args):
    game = os.path.join(TMP, 'pc', 'LBA')
    psdir = os.path.join(TMP, 'ps')
    check = None
    while args:
        if len(args) < 2 or args[0] not in ('-g', '-p', '-c'):
            usage()
        f, v = args.pop(0), args.pop(0)
        if f == '-g':
            game = v
        elif f == '-p':
            psdir = v
        else:
            check = v
    try:
        text = c(*table(game, psdir))
    except (Fail, IndexError, struct.error) as e:
        print('patches.py: %s' % e, file=sys.stderr)
        sys.exit(1)
    if check is None:
        sys.stdout.write(text)
        return
    with open(check) as f:
        if f.read() != text:
            print('patches.py: %s differs' % check, file=sys.stderr)
            sys.exit(1)


main(sys.argv[1:])
