"""lint: what of the tree's rules a program can check.

    python3 -I tools/lint.py [-c file.c...] [-p file.py...] [-b file...] [-- file...]

Each file after `--` holds no em dash (U+2014) and no en dash (U+2013),
which no code, comment or document of the tree uses, and each but the
documents (Markdown) names no file of the docs directory, which the
reader of the code may not have: its comments stand alone. A file named
there that is gone from the working tree is passed over, and so is a
binary file, one with a NUL in its first 8000 bytes, as git tells them
apart. Each -c file, C of our own, is laid out as The Practice of
Programming has it, a blank after if, for, while, switch, else and do,
before a brace that ends a line, and between a brace and else or while,
and holds NULL, never nil. Each -p file, a tool, parses as Python, opens
on its manual, a docstring, and imports nothing but the standard
library, as python3 -I runs it. Each -b file, C of LBA2's 1997 game,
names LBA2BUILD, which tells its two snapshots apart, June's LBA2.EXE
(970625) and July's LBA2.DOS (970703), in lines

    #if LBA2BUILD == 970625

alone, such a line, its #else and its #endif outside braces, and the
braces between them balanced, a file included into a body counted
from its own first line, and one gone from the working tree passed
over, as after `--`. Nothing is written: the tools are parsed, not
compiled. A finding is printed as file:line: what, and the status is 1
if there was one.
"""
import ast
import os
import re
import sys

DASHES = ('\u2014'.encode(), 'em dash'), ('\u2013'.encode(), 'en dash')
DOCS = re.compile(rb'\bdocs[/]')
LAYOUT = (
    (re.compile(rb'\b(if|for|while|switch)\('), 'no space after %s'),
    (re.compile(rb'\b(else|do)\{'), 'no space after %s'),
    (re.compile(rb'\}(else|while)\b'), 'no space before %s'),
    (re.compile(rb'\)(\{)\s*$'), 'no space before %s'),
)
NIL = re.compile(rb'\bnil\b')
FORK = '#if LBA2BUILD == 970625'
DIRECTIVE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|else|elif|endif)\b')

found = 0


def report(path, line, what):
    """A finding, printed and counted."""
    global found
    found += 1
    print('%s:%d: %s' % (path, line, what))


def lines(path):
    """A file's lines as bytes, numbered from 1."""
    with open(path, 'rb') as f:
        return enumerate(f.read().split(b'\n'), 1)


def binary(path):
    """Whether a file is binary, as git tells: a NUL in its first 8000 bytes."""
    with open(path, 'rb') as f:
        return b'\0' in f.read(8000)


def text(path):
    if not os.path.isfile(path) or binary(path):
        return
    doc = path.endswith('.md')
    for n, l in lines(path):
        for dash, name in DASHES:
            if dash in l:
                report(path, n, name)
        if not doc and DOCS.search(l):
            report(path, n, 'names a file of docs')


def cstyle(path):
    for n, l in lines(path):
        for pattern, what in LAYOUT:
            m = pattern.search(l)
            if m:
                report(path, n, what % m.group(1).decode())
        if NIL.search(l):
            report(path, n, 'nil, not NULL')


def tool(path):
    if not os.path.isfile(path):
        return
    with open(path, encoding='utf-8') as f:
        src = f.read()
    try:
        tree = ast.parse(src, path)
    except SyntaxError as e:
        report(path, e.lineno or 1, 'syntax error: %s' % e.msg)
        return
    if ast.get_docstring(tree) is None:
        report(path, 1, 'no docstring')
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names = [a.name for a in node.names]
        elif isinstance(node, ast.ImportFrom) and node.level == 0:
            names = [node.module]
        else:
            continue
        for name in names:
            if name.split('.')[0] not in sys.stdlib_module_names:
                report(path, node.lineno, 'imports %s, not in the standard library' % name)


def code(text):
    """C text with its comments, strings and character constants blanked,
    its lines kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            if j < n and text[j] == c:
                j += 1
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def forks(path):
    """LBA2BUILD in FORK lines alone, each fork outside braces, its
    branches' braces balanced."""
    if not os.path.isfile(path):
        return
    with open(path, encoding='utf-8', errors='replace') as f:
        raw = f.read().split('\n')
    depth = 0
    # for each open #if: its line, whether it is a fork, the depth at it,
    # the depth at the end of its first branch, which the others share
    stack = []
    for k, line in enumerate(code('\n'.join(raw)).split('\n'), 1):
        if 'LBA2BUILD' in raw[k - 1] and raw[k - 1].strip() != FORK:
            report(path, k, 'LBA2BUILD outside %s' % FORK)
        m = DIRECTIVE.match(line)
        if not m:
            depth += line.count('{') - line.count('}')
            continue
        what = m.group(1)
        if what.startswith('if'):
            fork = raw[k - 1].strip() == FORK
            if fork and depth != 0:
                report(path, k, '%s inside braces' % FORK)
            stack.append([k, fork, depth, None])
            continue
        if not stack:
            continue
        top = stack[-1]
        if top[1] and depth != top[2]:
            report(path, k, 'braces unbalanced in the fork of line %d' % top[0])
        if what in ('else', 'elif'):
            if top[3] is None:
                top[3] = depth
            depth = top[2]
        else:
            stack.pop()
            if top[3] is not None:
                depth = top[3]


def main():
    checks = {'-c': cstyle, '-p': tool, '-b': forks, '--': text}
    check = None
    args = sys.argv[1:]
    if not args or args[0] not in checks:
        sys.stderr.write('usage: lint [-c file.c...] [-p file.py...] [-b file...] [-- file...]\n')
        sys.exit(2)
    for a in args:
        if a in checks:
            check = checks[a]
        else:
            check(a)
    sys.exit(1 if found else 0)


if __name__ == '__main__':
    main()
