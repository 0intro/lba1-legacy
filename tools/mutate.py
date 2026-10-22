"""mutate.py: mutation testing of a C file against the oracles.

    python3 -I tools/mutate.py -d dir [-j n] [-r a-b] [-k report] [-T secs] [-c cc] file.c test...

Each mutant is file.c with one change in a function's body: a relation
turned (< and <=, > and >=, == and !=), an integer constant without a
suffix one more or one less, other than an array's size, a case's label
or one in a declaration, && and || swapped, a cast dropped, a statement
(a line ending in ';', other than a declaration, a return, a for, a
break, a continue, a case, a do's closing while, a USED() or an empty
one) dropped, (void)0 in its place, which compiles in every file.
Comments and preprocessor lines are left alone, and so is what lies
outside functions.

dir holds n workers (default the CPUs less two), each a copy of the
tree's Makefile and those of mk/, sys/, tools/, lib386/, sources/ and
re/ it holds as dir/wN/ has them, made afresh at each run, its objects
with it. For each mutant a worker writes it over its copy of file.c,
then runs each test in turn, a shell command run from its copy with REPO
naming it, O an object directory of its own, MAIN the tree mutate.py
runs in and LBATMP its resources. The mutant is killed by the first test
that fails or outlasts its seconds (-T, default 600), its processes all
killed then. A mutant that breaks the build is said so and counted
apart. -r restricts the mutants to those of lines a to b, -k to those a
report of an earlier run lists as surviving. -c gives a shell command
that compiles SRC into the object OBJ alone, as the tests build it: a
mutant whose object has the original's code, relocations and data, every
section's but those of the debugger, the comment and the notes, is the
same program, and survives as such, its tests skipped.

The report: every surviving mutant, its line and its change, those of
the original's code said so, then the counts. A survivor is either a gap
in the tests or an equivalent mutant, which the commit that takes the
file says why.

For example, from the tree's root, the mutants of sources/grille_a.c
against asmoracle's test of its module, on a copy of the OpenWatcom
build's image, LBATMP set:

    cp build/dos-386-openwatcom/lba0.elf build/dos-386-openwatcom/lba0.sym "$LBATMP"
    python3 -I tools/mutate.py -d ../lba1-tmp/mut -c 'gcc -m32 -std=c99 -O2 -g \\
        -mstackrealign -D_DEFAULT_SOURCE -DCDROM=1 -I$REPO/sys \\
        -funsigned-char -fno-strict-aliasing -fwrapv -c -o $OBJ $SRC' \\
        sources/grille_a.c 'make -s ORACLE=$O $O/asmoracle' \\
        '$O/asmoracle -g $LBATMP/pc/LBA $LBATMP/lba0.elf $LBATMP/lba0.sym grille'
"""
import concurrent.futures
import os
import re
import shutil
import signal
import subprocess
import sys

CASTS = r"\((?:u?int(?:8|16|32|64)_t|unsigned|int|uint32_t\*|uint8_t\*)\)"
# a declaration: a C type, or a name of a type as the tree spells them,
# capitalized, then a declarator
DECL = re.compile(r"^\s*(?:static\s+)?(?:const\s+)?(?:struct\s+)?"
                  r"(?:u?int(?:8|16|32|64)_t|u?intptr_t|size_t|int|char|short|long|void|unsigned|signed|[A-Z]\w*)"
                  r"(?:\s+|\s*\*)[\s*]*\w[^=(]*[;,]\s*$")


def usage():
    sys.stderr.write("usage: mutate.py -d dir [-j n] [-r a-b] [-k report] [-T secs] [-c cc] file.c test...\n")
    sys.exit(2)


def bodies(lines):
    """The lines inside functions' bodies, as a set of indices, and those in comments."""
    inside = set()
    depth = 0
    comment = False
    infunc = False
    for i, l in enumerate(lines):
        s = l
        if comment:
            if "*/" in s:
                comment = False
            continue
        if s.lstrip().startswith("/*") and "*/" not in s:
            comment = True
            continue
        if s.startswith("{") and depth == 0:
            infunc = i > 0 and lines[i-1].rstrip().endswith(")")
        if depth > 0 and infunc:
            inside.add(i)
        depth += s.count("{") - s.count("}")
        if depth == 0:
            infunc = False
    return inside


def strip_comment(s):
    """s without its /* */ comments and strings, the same length, to find what to change."""
    out = list(s)
    i = 0
    while i < len(s):
        if s.startswith("/*", i):
            j = s.find("*/", i + 2)
            j = len(s) if j < 0 else j + 2
            for k in range(i, j):
                out[k] = " "
            i = j
        elif s[i] == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            for k in range(i, min(j + 1, len(s))):
                out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def mutants(lines):
    """Every mutant: (line index, description, new line)."""
    out = []
    inside = bodies(lines)
    for i in sorted(inside):
        l = lines[i]
        if l.lstrip().startswith("#") or re.match(r"\s*\*(\s|/|$)", l) or l.lstrip().startswith("/*"):
            continue
        c = strip_comment(l)
        # relations
        for m in re.finditer(r"<=|>=|==|!=|(?<![<>=!-])<(?![<=])|(?<![<>=-])>(?![>=])", c):
            op = m.group(0)
            rep = {"<": "<=", "<=": "<", ">": ">=", ">=": ">", "==": "!=", "!=": "=="}[op]
            out.append((i, "%s to %s" % (op, rep), l[:m.start()] + rep + l[m.end():]))
        # logic
        for m in re.finditer(r"&&|\|\|", c):
            rep = "||" if m.group(0) == "&&" else "&&"
            out.append((i, "%s to %s" % (m.group(0), rep), l[:m.start()] + rep + l[m.end():]))
        # constants, other than array sizes and case labels
        if not re.match(r"\s*case\b", c) and not DECL.match(c):
            for m in re.finditer(r"(?<![\w.])(0x[0-9a-fA-F]+|\d+)(?![\w.])", c):
                if c[max(0, m.start()-1)] == "[" and c[m.end():m.end()+1] == "]" and "=" not in c[:m.start()]:
                    continue
                if m.group(1).startswith("0x"):
                    v, fmt = int(m.group(1), 16), "0x%x"
                elif len(m.group(1)) > 1 and m.group(1).startswith("0"):
                    v, fmt = int(m.group(1), 8), "0%o"
                else:
                    v, fmt = int(m.group(1)), "%d"
                out.append((i, "%s to %s" % (m.group(1), fmt % (v + 1)), l[:m.start()] + fmt % (v + 1) + l[m.end():]))
                if v > 0:
                    out.append((i, "%s to %s" % (m.group(1), fmt % (v - 1)), l[:m.start()] + fmt % (v - 1) + l[m.end():]))
        # casts
        for m in re.finditer(CASTS, c):
            out.append((i, "cast %s dropped" % m.group(0), l[:m.start()] + l[m.end():]))
        # statements
        t = c.strip()
        if t.endswith(";") and not DECL.match(c) and not t.startswith("return") and not t.startswith("for (") \
                and t not in ("break;", "continue;", ";") and not t.startswith("case ") and "{" not in t \
                and not t.startswith("}") and not t.startswith("USED("):
            out.append((i, "statement dropped", re.sub(r"\S.*", "(void)0;", l, count=1)))
    return out


def reported(path):
    """The (line, change) of each mutant a report lists as surviving."""
    out = set()
    with open(path) as f:
        for l in f:
            m = re.match(r"[^:]+:(\d+): survived(?:, the same code)?: (.*)$", l.rstrip("\n"))
            if m:
                out.add((int(m.group(1)), m.group(2)))
    return out


def setup(dirs, repo):
    """Each worker made afresh, the tree's copy as it stands and no
    object, which a worker kept from a run before would hold stale."""
    for w in dirs:
        shutil.rmtree(w, ignore_errors=True)
        for sub in ("mk", "sys", "tools", "lib386", "sources", "re"):
            if os.path.isdir(os.path.join(repo, sub)):
                shutil.copytree(os.path.join(repo, sub), os.path.join(w, sub), symlinks=True)
        shutil.copy2(os.path.join(repo, "Makefile"), os.path.join(w, "Makefile"))


def sections(obj):
    """The object's sections, but the debugger's, the comment and the notes."""
    r = subprocess.run(["objdump", "-h", obj], capture_output=True, text=True)
    names = []
    for l in r.stdout.splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s", l)
        if m and not m.group(1).startswith((".debug", ".comment", ".note")):
            names.append(m.group(1))
    return names


def objcode(cc, w, src, obj, main, lbatmp):
    """The object cc makes of src: its code, relocations and data as objdump shows them, or None."""
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    env = dict(os.environ, REPO=w, SRC=src, OBJ=obj, MAIN=main, LBATMP=lbatmp)
    if subprocess.run(cc, shell=True, cwd=w, env=env, stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL).returncode != 0:
        return None
    out = []
    js = [x for name in sections(obj) for x in ("-j", name)]
    for a in (["-dr"], ["-s"] + js):
        r = subprocess.run(["objdump"] + a + [obj], capture_output=True, text=True)
        out += [l for l in r.stdout.splitlines() if obj not in l]
    return out


def run(job):
    w, rel, i, desc, text, original, tests, timeout, main, lbatmp, cc, base = job
    path = os.path.join(w, rel)
    lines = list(original)
    lines[i] = text
    with open(path, "w") as f:
        f.writelines(lines)
    env = dict(os.environ, REPO=w, O=os.path.join(w, "obj"), MAIN=main, LBATMP=lbatmp)
    verdict = "survived"
    if cc is not None and objcode(cc, w, path, os.path.join(w, "obj", "mutant.o"), main, lbatmp) == base:
        tests = []
        verdict = "same"
    for n, t in enumerate(tests):
        p = subprocess.Popen(t, shell=True, cwd=w, env=env, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            rc = -1
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        p.wait()
        if rc != 0:
            verdict = "stillborn" if n == 0 else "killed"
            break
    with open(path, "w") as f:
        f.writelines(original)
    return i, desc, verdict


def main():
    args = sys.argv[1:]
    d = None
    jobs = max(1, (os.cpu_count() or 2) - 2)
    rng = None
    keep = None
    timeout = 600
    cc = None
    while args and args[0].startswith("-"):
        if len(args) < 2:
            usage()
        if args[0] == "-d":
            d = args[1]
        elif args[0] == "-j":
            jobs = int(args[1])
        elif args[0] == "-r":
            a, b = args[1].split("-")
            rng = (int(a), int(b))
        elif args[0] == "-k":
            keep = reported(args[1])
        elif args[0] == "-T":
            timeout = int(args[1])
        elif args[0] == "-c":
            cc = args[1]
        else:
            usage()
        args = args[2:]
    if d is None or len(args) < 2:
        usage()
    rel, tests = args[0], args[1:]
    repo = os.getcwd()
    lbatmp = os.environ.get("LBATMP", os.path.abspath(os.path.join(repo, "..", "lba1-tmp")))
    with open(rel) as f:
        original = f.readlines()
    ms = mutants(original)
    if rng is not None:
        ms = [m for m in ms if rng[0] <= m[0] + 1 <= rng[1]]
    if keep is not None:
        ms = [m for m in ms if (m[0] + 1, m[1]) in keep]
    dirs = [os.path.abspath(os.path.join(d, "w%d" % k)) for k in range(jobs)]
    setup(dirs, repo)
    base = None
    if cc is not None:
        base = objcode(cc, dirs[0], os.path.join(dirs[0], rel), os.path.join(dirs[0], "obj", "original.o"),
                       repo, lbatmp)
        if base is None:
            sys.exit("mutate.py: the original does not compile")
    work = [(dirs[k % jobs], rel, i, desc, text, original, tests, timeout, repo, lbatmp, cc, base)
            for k, (i, desc, text) in enumerate(ms)]
    counts = {"killed": 0, "survived": 0, "same": 0, "stillborn": 0}
    survivors = []
    # each worker's directory takes one mutant at a time
    queues = [[w for w in work if w[0] == dd] for dd in dirs]
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        def serial(q):
            return [run(job) for job in q]
        for res in ex.map(serial, queues):
            for i, desc, verdict in res:
                counts[verdict] += 1
                if verdict in ("survived", "same"):
                    survivors.append((i, desc, verdict))
    for i, desc, verdict in sorted(survivors):
        print("%s:%d: survived%s: %s" % (rel, i + 1, ", the same code" if verdict == "same" else "", desc))
    print("%s: %d mutants, %d killed, %d survived (%d the same code), %d did not build"
          % (rel, len(ms), counts["killed"], counts["survived"] + counts["same"], counts["same"], counts["stillborn"]))


if __name__ == "__main__":
    main()
