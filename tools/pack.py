"""pack: a folder written as an archive whose bytes depend only on the
folder's files and on the time given.

    python3 -I tools/pack.py -t time dir archive

pack writes the files of dir, and of the directories below it, into
archive: a zip file when the name ends in .zip, and a gzipped tar file
when it ends in .tar.gz or .tgz. pack refuses any other name, and a
file of dir other than a regular one, such as a symbolic link. The
archive holds the files under one directory, named as the archive
without its suffix, in the order of their names. Each takes the time
given, as RFC 3339 writes it in UTC (1994-10-14T10:43:15Z), the mode
0755 when its owner may run it and 0644 otherwise, and in a tar file
uid and gid 0, their names empty. Two runs on the same files write the
same bytes. pack writes the archive under a temporary name and renames
it at the end.

make dist writes the distribution's archive with it, at the time of
LBA.EXE's build.
"""
import calendar
import gzip
import io
import os
import stat
import sys
import tarfile
import time
import zipfile


def files(top):
    """The regular files below top, each its name with slashes and
    whether its owner may run it, sorted by name."""
    out = []
    for d, dirs, names in os.walk(top):
        for n in dirs + names:
            p = os.path.join(d, n)
            st = os.lstat(p)
            if stat.S_ISDIR(st.st_mode):
                continue
            if not stat.S_ISREG(st.st_mode):
                raise ValueError('%s: not a regular file' % p)
            rel = os.path.relpath(p, top).replace(os.sep, '/')
            out.append((rel, bool(st.st_mode & stat.S_IXUSR)))
    out.sort()
    return out


def mode(run):
    return 0o755 if run else 0o644


def read(top, name):
    with open(os.path.join(top, *name.split('/')), 'rb') as f:
        return f.read()


def writetar(out, top, base, fl, t):
    """The files as a gzipped tar file."""
    with gzip.GzipFile(filename='', mode='wb', fileobj=out, mtime=0) as z:
        with tarfile.open(fileobj=z, mode='w', format=tarfile.USTAR_FORMAT) as tf:
            for name, run in fl:
                b = read(top, name)
                ti = tarfile.TarInfo(base + '/' + name)
                ti.size = len(b)
                ti.mtime = t
                ti.mode = mode(run)
                ti.uid = ti.gid = 0
                ti.uname = ti.gname = ''
                tf.addfile(ti, io.BytesIO(b))


def writezip(out, top, base, fl, t):
    """The files as a zip file."""
    when = time.gmtime(t)[:6]
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as zf:
        for name, run in fl:
            zi = zipfile.ZipInfo(base + '/' + name, when)
            zi.create_system = 3
            zi.external_attr = (stat.S_IFREG | mode(run)) << 16
            zi.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(zi, read(top, name))


def pack(top, name, t):
    fl = files(top)
    if not fl:
        raise ValueError('%s: no file to pack' % top)
    base = os.path.basename(name)
    for suffix, write in (('.zip', writezip), ('.tar.gz', writetar), ('.tgz', writetar)):
        if base.endswith(suffix):
            base = base[:-len(suffix)]
            break
    else:
        raise ValueError('%s: want a name that ends in .zip, .tar.gz or .tgz' % name)
    tmp = name + '.tmp'
    try:
        with open(tmp, 'wb') as out:
            write(out, top, base, fl, t)
        os.replace(tmp, name)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def main():
    args = sys.argv[1:]
    if len(args) != 4 or args[0] != '-t':
        sys.stderr.write('usage: pack.py -t time dir archive\n')
        sys.exit(2)
    try:
        t = calendar.timegm(time.strptime(args[1], '%Y-%m-%dT%H:%M:%SZ'))
    except ValueError:
        sys.stderr.write('pack.py: -t %s: want a time such as 1994-10-14T10:43:15Z\n' % args[1])
        sys.exit(2)
    try:
        pack(args[2], args[3], t)
    except (OSError, ValueError) as e:
        sys.stderr.write('pack.py: %s\n' % e)
        sys.exit(1)


main()
