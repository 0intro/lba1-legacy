"""twins: the two ports' twin files held to each other.

usage: python3 -I tools/twins.py [-l] [-w] [sibling]

LBA1's port (../lba1) and LBA2's (../lba2) keep their own code in
sys/, the same file in each where the two games ask the same, and so
their C tests in test/. twins pairs each file of sys/ and each C file
of test/ that both trees hold under one name, and the files named in
Pairs, sets the
comments aside, maps the names that differ by game (Names), cuts each
C file into its top-level items (a function, a datum, a type, a
directive) and compares the two trees item by item. The sibling is
the other tree, ../lba2 from LBA1 and ../lba1 from LBA2 by default.

Each difference is a line: the file, by LBA1's name, the item, and how
it differs, "lba1" or "lba2" (that tree alone holds it) or "apart"
(both, differently), the same words whichever tree runs twins. tools/twins.allow lists the differences
the two trees keep, a line each, the file, the item, a tab and the
reason, "game:" for one the two games make, "align:" for one still to
align. An item of * stands for every difference of its kind in a file
the two games make apart whole. twins prints each difference absent from the list and each
entry of the list that differs no more, and fails on either. It also
fails where the sibling's tools/twins.py or tools/twins.allow differ
from this tree's: the two trees hold one tool and one list.

-l prints every difference, listed or not. -w writes the list anew
from the differences found, in order under the list's opening
comment, keeping the reason of each entry that still differs and
giving the others "align: to read".
"""

import os
import re
import sys

# The files the two trees name apart: this tree's name, the sibling's.
Pairs = [
    ('sys/lba.manifest', 'sys/lba2.manifest'),
    ('sys/lba.rc', 'sys/lba2.rc'),
]

# The names that differ by game, LBA2's to LBA1's, mapped before the
# comparison: identifiers and words whole.
Names = {
    'lba2': 'lba',
    'lba2bmp': 'lbabmp',
    'nlba2bmp': 'nlbabmp',
    'lba2cfg': 'lbacfg',
    'nlba2cfg': 'nlbacfg',
    'Vgah': 'Screen_Y',
    'Vgaw': 'Width',
    'FullScreen': 'FlagFullScreen',
}

Ident = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


def tree():
    """The tree this tool lies in."""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def isLBA2(root):
    return os.path.exists(os.path.join(root, 'sources', 'perso.c')) and \
        os.path.exists(os.path.join(root, 'lib386', 'system'))


def decomment(text):
    """The C text without its comments and strings' contents kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(' ')
            i = j
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def mapnames(text):
    return Ident.sub(lambda m: Names.get(m.group(0), m.group(0)), text)


def items(text):
    """The top-level items of C text, by name: name -> normalized text."""
    text = decomment(text)
    found = {}
    order = []

    def add(name, body):
        body = ' '.join(body.split())
        if not body:
            return
        k = name
        n = 2
        while k in found:
            k = '%s#%d' % (name, n)
            n += 1
        found[k] = body
        order.append(k)

    lines = text.split('\n')
    buf = []
    depth = 0
    for line in lines:
        if depth == 0 and not ''.join(buf).strip() and line.lstrip().startswith('#'):
            d = line.split()
            name = ' '.join(d[:2]) if len(d) > 1 else d[0]
            add(name, line)
            buf = []
            continue
        buf.append(line)
        for ch in line:
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
        joined = '\n'.join(buf)
        if depth == 0 and (joined.rstrip().endswith(';') or joined.rstrip().endswith('}')):
            stripped = joined.strip()
            if stripped:
                add(itemname(stripped), stripped)
            buf = []
    if '\n'.join(buf).strip():
        add('(end)', '\n'.join(buf))
    return found, order


def toplevel(body):
    """body with what lies within its braces left out, the braces kept."""
    out = []
    depth = 0
    for ch in body:
        if ch == '{':
            if depth == 0:
                out.append('{')
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                out.append('}')
        elif depth == 0:
            out.append(ch)
    return ' '.join(''.join(out).split())


def itemname(body):
    """An item's name: a function's, a declaration's, a type's or a datum's."""
    top = toplevel(body)
    m = re.match(r'^(.*?)\b([A-Za-z_]\w*)\s*\((?:[^()]|\([^()]*\))*\)\s*\{\}$', top)
    if m:
        return m.group(2)
    if top.startswith('enum'):
        first = re.search(r'\{\s*([A-Za-z_]\w*)', body)
        return 'enum ' + (first.group(1) if first else '?')
    if top.startswith('typedef'):
        return 'typedef ' + Ident.findall(top)[-1]
    m = re.match(r'^struct\s+(\w+)\s*\{\}\s*;$', top)
    if m:
        return 'struct ' + m.group(1)
    if top.endswith(');') and '(' in top and '=' not in top:
        m = re.search(r'([A-Za-z_]\w*)\s*\(', top)
        return 'decl ' + (m.group(1) if m else '?')
    m = re.search(r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*(?:=|;|,)', top)
    return m.group(1) if m else '?'


def pairs(here, there):
    """The twin files: (this tree's path, the sibling's)."""
    out = []
    for d in ('sys', 'test'):
        a = os.path.join(here, d)
        b = os.path.join(there, d)
        if not os.path.isdir(a) or not os.path.isdir(b):
            continue
        for root, dirs, files in os.walk(a):
            dirs.sort()
            rel = os.path.relpath(root, here)
            for f in sorted(files):
                p = os.path.join(rel, f)
                if d == 'test' and not f.endswith('.c'):
                    continue
                if os.path.isfile(os.path.join(there, p)):
                    out.append((p, p))
    lba2 = isLBA2(here)
    for p, q in Pairs:
        a, b = (q, p) if lba2 else (p, q)
        if os.path.isfile(os.path.join(here, a)) and os.path.isfile(os.path.join(there, b)):
            out.append((a, b))
    return out


def differences(here, there):
    """Each difference: (file, item, how)."""
    out = []
    lba2 = isLBA2(here)
    for a, b in pairs(here, there):
        ta = open(os.path.join(here, a), encoding='utf-8', errors='replace').read()
        tb = open(os.path.join(there, b), encoding='utf-8', errors='replace').read()
        if lba2:
            ta = mapnames(ta)
        else:
            tb = mapnames(tb)
        name = a if not lba2 else b
        if not name.endswith(('.c', '.h')):
            if ' '.join(ta.split()) != ' '.join(tb.split()):
                out.append((name, '(file)', 'apart'))
            continue
        ia, oa = items(ta)
        ib, ob = items(tb)
        here_is = 'lba2' if lba2 else 'lba1'
        there_is = 'lba1' if lba2 else 'lba2'
        for k in oa:
            if k not in ib:
                out.append((name, k, here_is))
            elif ia[k] != ib[k]:
                out.append((name, k, 'apart'))
        for k in ob:
            if k not in ia:
                out.append((name, k, there_is))
    return out


def readallow(path):
    allow = {}
    if not os.path.exists(path):
        return allow
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if not line or line.startswith('#'):
            continue
        key, _, reason = line.partition('\t')
        allow[key] = reason
    return allow


def main():
    args = sys.argv[1:]
    listall = '-l' in args
    write = '-w' in args
    args = [a for a in args if a not in ('-l', '-w')]
    here = tree()
    sibling = args[0] if args else os.path.join(os.path.dirname(here), 'lba1' if isLBA2(here) else 'lba2')
    if not os.path.isdir(sibling):
        sys.exit('twins: no sibling tree at %s' % sibling)
    there = os.path.abspath(sibling)
    fails = 0
    for f in ('tools/twins.py', 'tools/twins.allow'):
        a, b = os.path.join(here, f), os.path.join(there, f)
        if os.path.exists(a) and os.path.exists(b) and open(a, 'rb').read() != open(b, 'rb').read():
            print('twins: %s differs from the sibling\'s' % f)
            fails += 1
    diffs = differences(here, there)
    allowpath = os.path.join(here, 'tools', 'twins.allow')
    allow = readallow(allowpath)
    diffs.sort()
    keys = ['%s %s %s' % d for d in diffs]
    if write:
        header = []
        if os.path.exists(allowpath):
            for line in open(allowpath, encoding='utf-8'):
                if not line.startswith('#'):
                    break
                header.append(line)
        lines = set()
        for d, k in zip(diffs, keys):
            w = '%s * %s' % (d[0], d[2])
            if w in allow:
                lines.add('%s\t%s\n' % (w, allow[w]))
            else:
                lines.add('%s\t%s\n' % (k, allow.get(k, 'align: to read')))
        with open(allowpath, 'w', encoding='utf-8') as f:
            f.writelines(header + sorted(lines))
        return
    def listed(d):
        return '%s %s %s' % d in allow or '%s * %s' % (d[0], d[2]) in allow

    for d, k in zip(diffs, keys):
        if listall or not listed(d):
            print(k + ('' if listed(d) else '\tnot listed'))
            fails += not listed(d)
    wild = {'%s * %s' % (d[0], d[2]) for d in diffs}
    for k in allow:
        if k not in keys and k not in wild:
            print(k + '\tlisted, the same now')
            fails += 1
    sys.exit(1 if fails else 0)


main()
