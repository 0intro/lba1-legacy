"""mkdefs: a module's .def, the prototypes of the functions its source
defines, in the form of Watcom's -v files, from gcc's -aux-info.

    python3 -I tools/mkdefs.py [-u header] [-n disk.aux] [-d dos.aux [-c cd.aux]] file.aux source.c > source.def

gcc -aux-info lists every function declared or defined while it
compiles, each line opened by a comment naming the file, the line, N or
O (a prototype or an old-style definition) and C or F (declared or
defined). The functions source.c defines, but the static ones, are
written in their order as "extern type name(type ,type );": each
parameter's name dropped, a space after each type but a pointer's, as
the 1994 files had them. A function without parameters takes void.
With -u, a name the header renames by "#define name other" (as
sys/compat.h renames Watcom's functions) is written as the source has
it. With -n, disk.aux is the -aux-info of the same source
compiled without CDROM, the disk version: a function it lacks is
written inside #ifndef DOS, since the 1994 .def files were written by a
compile of the disk version, and the DOS build compiles the CD version
against those prototypes alone, as the 1994 tree was compiled. With -d,
dos.aux is the -aux-info of the same source compiled as the DOS build
compiles it (DOS and CDROM defined, as C89): a function it lacks,
defined outside DOS alone, is written inside #ifndef DOS too, and one
whose prototype differs there is written twice, the DOS build's under
#ifdef DOS and the other under #else. dos.aux is the build with the
fixes, FIXES defined. With -c, cd.aux is the same compile without
FIXES, the CD's code: a function only the fixes define is written
inside #ifdef FIXES, and one whose prototype the fixes change is written
twice, the fixes' under #ifdef FIXES and the CD's under #else.
"""
import re
import sys

LINE = re.compile(r'^/\* (?P<file>.+):\d+:[NO]F \*/ (?P<decl>.*?;)')
DECL = re.compile(r'^(?P<ret>.*?)(?P<name>\w+) \((?P<params>.*)\);$')


def ptype(p):
    """A parameter's type, its name dropped."""
    p = p.strip()
    m = re.match(r'^(.*?)\(\s*\*\s*\w+\s*\)(.*)$', p)
    if m:
        return '%s(*)%s' % (m.group(1), m.group(2))
    m = re.match(r'^(.*[\s*])\w+$', p)
    if not m:
        sys.exit('mkdefs: no name in parameter %r' % p)
    return m.group(1).rstrip()


def proto(decl):
    """A definition's line as the .def writes it, or None for a static one."""
    decl = decl.strip()
    if not decl.startswith('extern '):
        return None
    m = DECL.match(decl[len('extern '):])
    if not m:
        sys.exit('mkdefs: cannot read %r' % decl)
    ret = m.group('ret').strip()
    params = m.group('params').strip()
    types = []
    if params not in ('', 'void'):
        types = [ptype(p) for p in params.split(',')]
    args = ','.join(t if t.endswith('*') else t + ' ' for t in types) or 'void'
    sep = '' if ret.endswith('*') else ' '
    return 'extern %s%s%s(%s);' % (ret, sep, m.group('name'), args)


def renames(header):
    """The names a header's "#define name other" lines rename, by their new name."""
    r = {}
    for line in open(header, encoding='latin-1'):
        m = re.match(r'^#define\s+(\w+)\s+(\w+)\s*$', line)
        if m:
            r[m.group(2)] = m.group(1)
    return r


def defined(aux, src):
    """The prototypes of the functions src defines, as the .def writes them."""
    protos = []
    for line in open(aux, encoding='latin-1'):
        m = LINE.match(line)
        if not m or m.group('file') != src:
            continue
        p = proto(m.group('decl'))
        if p is not None:
            protos.append(p)
    return protos


def name(p):
    """The name of the function a prototype declares."""
    return re.search(r'(\w+)\(', p).group(1)


def main():
    args = sys.argv[1:]
    undo = {}
    disk = None
    dos = None
    cd = None
    while len(args) > 1 and args[0] in ('-u', '-n', '-d', '-c'):
        if args[0] == '-u':
            undo = renames(args[1])
        elif args[0] == '-n':
            disk = args[1]
        elif args[0] == '-d':
            dos = args[1]
        else:
            cd = args[1]
        args = args[2:]
    if len(args) != 2 or (cd is not None and dos is None):
        sys.exit('usage: mkdefs [-u header] [-n disk.aux] [-d dos.aux [-c cd.aux]] file.aux source.c')
    aux, src = args
    indisk = None
    if disk is not None:
        indisk = {name(p) for p in defined(disk, src)}
    indos = None
    if dos is not None:
        indos = {name(p): p for p in defined(dos, src)}
    incd = None
    if cd is not None:
        incd = {name(p): p for p in defined(cd, src)}
    # the guard open around the lines written: None, DOS or FIXES
    guard = None
    def setguard(g):
        nonlocal guard
        if g != guard:
            if guard is not None:
                print('#endif')
            if g == 'DOS':
                print('#ifndef\tDOS')
            elif g == 'FIXES':
                print('#ifdef\tFIXES')
            guard = g
    def rename(p, n):
        return p.replace(n + '(', undo[n] + '(', 1) if p is not None and n in undo else p
    for p in defined(aux, src):
        n = name(p)
        hidden = ((indisk is not None and n not in indisk)
                  or (indos is not None and n not in indos))
        fixesonly = not hidden and incd is not None and n not in incd
        setguard('DOS' if hidden else 'FIXES' if fixesonly else None)
        alt = None
        if not hidden and indos is not None and indos[n] != p:
            alt = indos[n]
        cdp = None
        if not hidden and not fixesonly and incd is not None and incd[n] != indos[n]:
            cdp = incd[n]
        p, alt, cdp = rename(p, n), rename(alt, n), rename(cdp, n)
        if cdp is not None:
            if alt is None:
                print('#ifdef\tFIXES')
                print(p)
                print('#else')
                print(cdp)
                print('#endif')
            else:
                print('#ifdef\tDOS')
                print('#ifdef\tFIXES')
                print(alt)
                print('#else')
                print(cdp)
                print('#endif')
                print('#else')
                print(p)
                print('#endif')
        elif alt is None:
            print(p)
        else:
            print('#ifdef\tDOS')
            print(alt)
            print('#else')
            print(p)
            print('#endif')
    setguard(None)
    if incd is not None:
        for n in incd:
            if n not in indos:
                sys.exit('mkdefs: %s: %s is defined by the CD\'s code alone' % (src, n))


main()
