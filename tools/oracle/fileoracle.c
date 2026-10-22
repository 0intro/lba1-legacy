#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "dat.h"
#include "fns.h"
#include "dos.h"
#include "dll.h"
#include "abi.h"
#include "pc.h"
#include "dosfs.h"

/*
 * fileoracle: the game's files as lib386/lib_sys/files.c and files_a.asm
 * give them through DOS, against sys/files.c, the port's, at the
 * interface the game calls: OpenRead, OpenWrite, Read, Write, Close,
 * Seek, Delete, FileSize, AddExt, Touch and the search for files
 * (_dos_findfirst, _dos_findnext).
 *
 *	fileoracle -d dir lba0.elf lba0.sym
 *
 * lba0.elf is a DOS build, as tools/le2elf.py makes it, and lba0.sym
 * names its procedures and data (tools/wmap2sym.py of its link map).
 * Its code, Watcom's run-time among it, runs on pc.c's PC and dosfs.c's
 * DOS, whose int 21h keeps its files in dir/dos, the game's directory,
 * current, as C:\LBA is. sys/files.c reads the same files, the
 * game's data bound at the game's root, dir/game, the saves and the
 * voices' cache in its preferences, dir/pref, where it writes, as the
 * port runs on a CD's copy.
 *
 *	random	10000 of the game's calls at random on 14 names, at the
 *		root and in VOX, some there at the start: a file opened
 *		to read and read, one opened to write and written, either
 *		sought within it, closed, sized, deleted when closed,
 *		touched, and the searches
 *	game	the game's sequences: the voices copied to the disk in
 *		blocks of 64 KB and read as message.c reads them, the saves
 *		written, listed and deleted as gamemenu.c does, and lba.cfg
 *		written again
 *	addext	AddExt on the game's paths
 *
 * The random and game parts run a second time with a writer, as the port
 * plays: the root read once into a view and the files' writes done as
 * jobs, which the oracle runs in the writer thread's place, a few at
 * random after each call of random and at each wait, as sys/sdl.c's
 * thread would.
 *
 * After each call of random the results must be the same: a handle, 0
 * for a failure, the bytes read, a size, a place, the files a search
 * finds and their sizes. game compares the voices' reads, their counts
 * and bytes, and the place of their seek, needs a handle on both sides
 * for each save, and compares the saves' searches and their deletes.
 * After random and game, and every 500 calls of random while its files
 * are all closed, the files must be the same, name by name, byte for
 * byte. Then the assembly alone:
 *
 *	convention	Touch keeps the registers C keeps
 *
 * What the port does otherwise on purpose is let pass:
 *
 *	the files a search finds in the port sorted by name, where DOS
 *	gives them in the directory's order: they are compared as sets
 *	a root name in its own case in the port, in upper case in DOS
 *	a file's date, the host's clock's in the port
 *	the game's data, lba.cfg and FILE3D.HQR here, which the port
 *	keeps where they lie and DOS deletes when asked: the game deletes
 *	its saves and voices alone
 *	a file read through a handle opened to write, or written through
 *	one opened to read, or sought past its end: the game reads through
 *	a handle opened to read, writes through one opened to write, and
 *	seeks within a file
 *	a Write of 0 bytes short of a file's end, which DOS takes to cut the
 *	file there: the game writes its files from their start to their end
 *	a root file opened to write, which the port holds in memory to its
 *	close where DOS has it on the disk from its open: the game
 *	searches and sizes files between its writes
 *
 * Each part runs in a process of its own, ended after 600 seconds, its
 * differences reported, the first 20 in full, and the end is
 * "fileoracle: ok", or "fileoracle: N failed" and status 1.
 */

enum
{
	Npath = 1024,
	Nopen = 10,
	Nname = 14,
	Rounds = 10000,
	Check = 500,	/* calls between two comparisons of the files */
	Maxsize = 200000,
	Maxfile = 150000,	/* a file grows to this at most */
	Block = 65536,	/* CopyFileCD_HD's */
	Ndta = 43,
};

/* the assembly's procedures, by lba0.sym */
static struct
{
	uintptr_t	openread;
	uintptr_t	openwrite;
	uintptr_t	read;
	uintptr_t	write;
	uintptr_t	close;
	uintptr_t	seek;
	uintptr_t	delete;
	uintptr_t	filesize;
	uintptr_t	addext;
	uintptr_t	touch;
	uintptr_t	findfirst;
	uintptr_t	findnext;
} as;

/* sys/files.c's interface, which lib386/lib_sys/lib_sys.h declares for the game */
int32_t	OpenRead(char*);
int32_t	OpenWrite(char*);
uint32_t	Read(int32_t, void*, uint32_t);
uint32_t	Write(int32_t, void*, uint32_t);
void	Close(int32_t);
int32_t	Seek(int32_t, int32_t, int32_t);
int32_t	Delete(char*);
uint32_t	FileSize(char*);
void	AddExt(char*, char*);
void	Touch(char*);

static char *dir;
static char dosdir[Npath/2];	/* dir/dos, C:\LBA */
static char gamedir[Npath/2];
static char prefdir[Npath/2];
static int withwriter;	/* the port's writes go through its writer */
static uint8_t *abuf;	/* the assembly's buffers, in the image's reach */
static uint8_t *pbuf;

/* the names the parts call on, as the game writes them */
static char *names[Nname] = {
	"LBA.CFG", "S1.LBA", "S2.LBA", "S3.LBA", "NEW1.LBA", "new2.lba", "FILE3D.HQR",
	"A.TXT", "VOX\\EN_000.VOX", "VOX\\EN_001.VOX", "vox\\en_002.vox", "VOX\\NEW.VOX",
	"VOX\\EN_000.ORD", "SAVE.LBA",
};
static char *patterns[] = {"*.LBA", "*.*", "VOX\\*.VOX", "S?.LBA", "VOX\\*.*", "*.HQR", "NONE*.X"};

static uint32_t
rnd(void)
{
	static uint32_t x = 0x7f4a7c15;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

static void
spit(char *path, uint8_t *b, int n)
{
	FILE *f;

	f = fopen(path, "wb");
	if (f == NULL || (int)fwrite(b, 1, n, f) != n)
		fatal("%s: cannot write", path);
	fclose(f);
}

/*
 * The writer's lock, for files.c, the oracle standing in for its
 * thread: a wait runs the next job, and so, at random, may the taking
 * of the lock, as the thread would finish one between two calls.
 */
static void
writehook(int op)
{
	if (op == Wwait && !stepwrite())
		fatal("a wait with no job to run");
	if (op == Wlock && rnd() % 4 == 0)
		stepwrite();
}

/* The writer's jobs run, a few at random, as its thread would between two calls. */
static void
writes(void)
{
	int n;

	if (!withwriter)
		return;
	for (n = rnd() % 3; n > 0 && stepwrite(); n--)
		;
}

/* path and what lies below it removed, as rm -rf removes them: 0, or -1. */
static int
rmtree(char *path)
{
	char sub[Npath];
	struct dirent *e;
	struct stat st;
	DIR *d;
	int r;

	if (lstat(path, &st) < 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISDIR(st.st_mode))
		return unlink(path);
	d = opendir(path);
	if (d == NULL)
		return -1;
	r = 0;
	while ((e = readdir(d)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		if (snprintf(sub, sizeof sub, "%s/%s", path, e->d_name) >= (int)sizeof sub || rmtree(sub) < 0)
			r = -1;
	}
	closedir(d);
	return r == 0 ? rmdir(path) : -1;
}

/* The start's files, the same in dir/dos and dir/game, the preferences empty. */
static void
fresh(void)
{
	static char *start[] = {"LBA.CFG", "FILE3D.HQR", "S1.LBA", "S2.LBA", "S3.LBA",
		"VOX/EN_000.VOX", "VOX/EN_001.VOX"};
	char path[Npath];
	uint8_t b[20000];
	int i, j, n;

	if (rmtree(dosdir) < 0 || rmtree(gamedir) < 0 || rmtree(prefdir) < 0)
		fatal("cannot clear %s", dir);
	for (i = 0; i < 3; i++) {
		snprintf(path, sizeof path, "%s", i == 0 ? dosdir : i == 1 ? gamedir : prefdir);
		if (mkdir(path, 0777) < 0)
			fatal("%s: cannot make it", path);
		strcat(path, "/VOX");
		if (mkdir(path, 0777) < 0)
			fatal("%s: cannot make it", path);
	}
	for (i = 0; i < (int)nelem(start); i++) {
		n = 100 + rnd() % (sizeof b - 100);
		for (j = 0; j < n; j++)
			b[j] = rnd();
		snprintf(path, sizeof path, "%s/%s", dosdir, start[i]);
		spit(path, b, n);
		snprintf(path, sizeof path, "%s/%s", i < 2 ? gamedir : prefdir, start[i]);
		spit(path, b, n);
	}
	dosfsinit(dosdir);
	setpref(prefdir);
	if (bindpath(0, "", gamedir, (char*)b, sizeof b) < 0)
		fatal("%s", (char*)b);
	setwriter(withwriter ? writehook : NULL);
}

/* The assembly's call, its strings copied into the image's reach. */
static int32_t
acallname(uintptr_t fn, char *name)
{
	strcpy((char*)abuf, name);
	return acall(fn, 1, (int32_t)(uintptr_t)abuf);
}

/* Both sides' files the same, every name found by a search of each directory. */
static int
samefiles(void)
{
	static char *dirs[] = {"*.*", "VOX\\*.*"};
	static char *dname[] = {"", "VOX\\"};
	char pat[64], name[300];
	struct find_t pf;
	uint8_t a[Maxsize], *ft;
	int d, r, k, na, np;
	int32_t h;
	uint32_t n;

	for (d = 0; d < 2; d++) {
		snprintf(pat, sizeof pat, "%s", dirs[d]);
		ft = abuf + 512;
		strcpy((char*)abuf, pat);
		na = 0;
		for (r = acall(as.findfirst, 3, (int32_t)(uintptr_t)abuf, 0, (int32_t)(uintptr_t)ft);
		    r == 0; r = acall(as.findnext, 1, (int32_t)(uintptr_t)ft)) {
			if (ft[21] & 0x10)
				continue;
			na++;
			snprintf(name, sizeof name, "%s%s", dname[d], (char*)ft + 30);
			snprintf(what, sizeof what, "the files: %s", name);
			k = acallname(as.filesize, name);
			n = FileSize(name);
			if ((uint32_t)k != n) {
				fail("the assembly's size %d, ours %u", k, n);
				return 0;
			}
			h = OpenRead(name);
			if (h == 0) {
				fail("ours lacks it");
				return 0;
			}
			if (Read(h, pbuf, Maxsize) != n) {
				fail("ours reads other than %u bytes", n);
				Close(h);
				return 0;
			}
			Close(h);
			strcpy((char*)abuf, name);
			h = acall(as.openread, 1, (int32_t)(uintptr_t)abuf);
			if (h == 0) {
				fail("the assembly cannot open it");
				return 0;
			}
			k = acall(as.read, 3, h, (int32_t)(uintptr_t)(abuf + 1024), (int32_t)n);
			acall(as.close, 1, h);
			memmove(a, abuf + 1024, n);
			if ((uint32_t)k != n || memcmp(a, pbuf, n) != 0) {
				fail("the bytes differ");
				return 0;
			}
			strcpy((char*)abuf, pat);
		}
		np = 0;
		snprintf(pat, sizeof pat, "%s", dirs[d]);
		for (r = _dos_findfirst(pat, 0, &pf); r == 0; r = _dos_findnext(&pf))
			if (!(pf.attrib & 0x10))
				np++;
		if (na != np) {
			snprintf(what, sizeof what, "the files of %s", dirs[d]);
			fail("the assembly's %d files, ours %d", na, np);
			return 0;
		}
	}
	return 1;
}

/* A file a search found, its name and size. */
typedef struct Found Found;

struct Found
{
	char	name[13];
	uint32_t	size;
};

static int
foundcmp(const void *a, const void *b)
{
	return strcasecmp(((Found*)a)->name, ((Found*)b)->name);
}

/* A search on both sides: the same names, compared as sets, and their sizes, which the voices' cache keeps. */
static void
samesearch(char *pat)
{
	Found af[64], pf[64];
	struct find_t ft;
	uint8_t *dt;
	int r, na, np, i;

	snprintf(what, sizeof what, "the search for %s", pat);
	dt = abuf + 512;
	strcpy((char*)abuf, pat);
	na = 0;
	for (r = acall(as.findfirst, 3, (int32_t)(uintptr_t)abuf, 0, (int32_t)(uintptr_t)dt);
	    r == 0 && na < 64; r = acall(as.findnext, 1, (int32_t)(uintptr_t)dt)) {
		snprintf(af[na].name, 13, "%.12s", (char*)dt + 30);
		af[na++].size = get32(dt + 26);
	}
	np = 0;
	for (r = _dos_findfirst(pat, 0, &ft); r == 0 && np < 64; r = _dos_findnext(&ft)) {
		snprintf(pf[np].name, 13, "%.12s", ft.name);
		pf[np++].size = ft.size;
	}
	qsort(af, na, sizeof af[0], foundcmp);
	qsort(pf, np, sizeof pf[0], foundcmp);
	if (na != np) {
		fail("the assembly finds %d (%s%s%s), ours %d (%s%s%s)", na, na > 0 ? af[0].name : "",
			na > 1 ? " " : "", na > 1 ? af[1].name : "", np, np > 0 ? pf[0].name : "",
			np > 1 ? " " : "", np > 1 ? pf[1].name : "");
		return;
	}
	for (i = 0; i < na; i++)
		if (strcasecmp(af[i].name, pf[i].name) != 0 || af[i].size != pf[i].size) {
			fail("the assembly finds %s of %u bytes, ours %s of %u", af[i].name, af[i].size,
				pf[i].name, pf[i].size);
			return;
		}
}

/* A handle open on both sides, and how. */
typedef struct Open Open;

struct Open
{
	int32_t	ah;
	int32_t	ph;
	int	wr;
	char	*name;
	uint32_t	len;	/* bytes written so far, a handle to write's */
	uint32_t	pos;
};

static void
atrandom(void *v)
{
	Open open[Nopen];
	int nopen, i, j, k, ok;
	int32_t a, p;
	uint32_t n, m;
	char *name;
	Open *o;

	withwriter = v != NULL;
	fresh();
	nopen = 0;
	for (i = 0; i < Rounds; i++) {
		name = names[rnd() % Nname];
		switch (rnd() % 10) {
		case 0:
			if (nopen == Nopen)
				break;
			for (j = 0; j < nopen; j++)
				if (strcmp(open[j].name, name) == 0)
					break;
			if (j < nopen)
				break;
			k = rnd() % 3 == 0;
			snprintf(what, sizeof what, "call %d, %s(%s)", i, k ? "OpenWrite" : "OpenRead", name);
			a = acallname(k ? as.openwrite : as.openread, name);
			p = k ? OpenWrite(name) : OpenRead(name);
			if (a != p) {
				fail("the assembly's handle %d, ours %d", a, p);
				if (a != 0)
					acall(as.close, 1, a);
				if (p != 0)
					Close(p);
				break;
			}
			if (a == 0)
				break;
			o = &open[nopen++];
			o->ah = a;
			o->ph = p;
			o->wr = k;
			o->name = name;
			o->len = 0;
			o->pos = 0;
			if (!k)
				o->len = FileSize(name);
			break;
		case 1:
		case 2:
			if (nopen == 0)
				break;
			o = &open[rnd() % nopen];
			if (o->wr) {
				n = rnd() % 3000;
				if (n == 0 && o->pos != o->len)
					n = 1;
				if (o->pos + n > Maxfile)
					break;
				for (j = 0; j < (int)n; j++)
					abuf[1024 + j] = pbuf[j] = rnd();
				snprintf(what, sizeof what, "call %d, Write(%s, %u) at %u", i, o->name, n, o->pos);
				a = acall(as.write, 3, o->ah, (int32_t)(uintptr_t)(abuf + 1024), (int32_t)n);
				m = Write(o->ph, pbuf, n);
				if ((uint32_t)a != m)
					fail("the assembly wrote %d, ours %u", a, m);
				o->pos += n;
				if (o->len < o->pos)
					o->len = o->pos;
			} else {
				n = rnd() % 8 == 0 ? 0xffffffff : rnd() % 5000;
				snprintf(what, sizeof what, "call %d, Read(%s, %d) at %u", i, o->name, (int)n, o->pos);
				a = acall(as.read, 3, o->ah, (int32_t)(uintptr_t)(abuf + 1024), (int32_t)n);
				m = Read(o->ph, pbuf, n);
				if ((uint32_t)a != m || memcmp(abuf + 1024, pbuf, m) != 0)
					fail("the assembly read %d, ours %u", a, m);
				o->pos += m;
			}
			break;
		case 3:
			if (nopen == 0)
				break;
			o = &open[rnd() % nopen];
			k = rnd() % 3;
			n = o->len + 1;
			j = k == 0 ? (int)(rnd() % n) : k == 1 ? -(int)(rnd() % (o->pos + 1)) : -(int)(rnd() % n);
			snprintf(what, sizeof what, "call %d, Seek(%s, %d, %d) from %u", i, o->name, j, k, o->pos);
			a = acall(as.seek, 3, o->ah, j, k);
			p = Seek(o->ph, j, k);
			if (a != p)
				fail("the assembly at %d, ours %d", a, p);
			if (p >= 0)
				o->pos = p;
			break;
		case 4:
			if (nopen == 0)
				break;
			k = rnd() % nopen;
			o = &open[k];
			snprintf(what, sizeof what, "call %d, Close(%s)", i, o->name);
			acall(as.close, 1, o->ah);
			Close(o->ph);
			open[k] = open[--nopen];
			break;
		case 5:
			ok = 1;
			for (j = 0; j < nopen; j++)
				if (open[j].wr)
					ok = 0;
			if (!ok)
				break;
			snprintf(what, sizeof what, "call %d, FileSize(%s)", i, name);
			a = acallname(as.filesize, name);
			m = FileSize(name);
			if ((uint32_t)a != m)
				fail("the assembly's %d, ours %u", a, m);
			break;
		case 6:
			for (j = 0; j < nopen; j++)
				if (strcmp(open[j].name, name) == 0)
					break;
			if (j < nopen || strcmp(name, "LBA.CFG") == 0 || strcmp(name, "FILE3D.HQR") == 0)
				break;
			snprintf(what, sizeof what, "call %d, Delete(%s)", i, name);
			a = acallname(as.delete, name);
			p = Delete(name);
			if (a != p)
				fail("the assembly's %d, ours %d", a, p);
			break;
		case 7:
			for (j = 0; j < nopen; j++)
				if (strcmp(open[j].name, name) == 0)
					break;
			if (j < nopen || FileSize(name) == 0)
				break;
			snprintf(what, sizeof what, "call %d, Touch(%s)", i, name);
			acallname(as.touch, name);
			Touch(name);
			break;
		default:
			for (j = 0; j < nopen; j++)
				if (open[j].wr)
					break;
			if (j == nopen)
				samesearch(patterns[rnd() % nelem(patterns)]);
			break;
		}
		writes();
		if (nfail > 0)
			break;
		if (i % Check == Check - 1 && nopen == 0 && !samefiles())
			break;
	}
	for (j = 0; j < nopen; j++) {
		acall(as.close, 1, open[j].ah);
		Close(open[j].ph);
	}
	if (withwriter)
		endwrites();
	if (nfail == 0)
		samefiles();
	printf("random%s: %d calls, %s\n", withwriter ? ", a writer" : "", i,
		nfail == 0 ? "the same" : "differ");
}

/* CopyFileCD_HD's copy of n bytes to name, in blocks of 64 KB, the last one short, maybe of 0. */
static void
copyvoice(char *name, uint32_t n)
{
	int32_t a, p;
	uint32_t k, m, w;

	snprintf(what, sizeof what, "the copy of %s, %u bytes", name, n);
	a = acallname(as.openwrite, name);
	p = OpenWrite(name);
	if (a != p || a == 0) {
		fail("the assembly's handle %d, ours %d", a, p);
		return;
	}
	for (k = 0; k <= n / Block; k++) {
		m = k < n / Block ? Block : n % Block;
		for (w = 0; w < m; w++)
			abuf[1024 + w] = pbuf[w] = rnd();
		a = acall(as.write, 3, a, (int32_t)(uintptr_t)(abuf + 1024), (int32_t)m);
		w = Write(p, pbuf, m);
		if ((uint32_t)a != w)
			fail("block %u: the assembly wrote %d, ours %u", k, a, w);
		a = p;
	}
	acall(as.close, 1, a);
	Close(p);
	acallname(as.touch, name);
	Touch(name);
}

static void
game(void *v)
{
	static char *voices[] = {"VOX\\EN_003.VOX", "VOX\\EN_004.VOX", "VOX\\EN_GAM.VOX"};
	static uint32_t sizes[] = {Block, 2*Block + 1234, 70000};
	char name[32];
	int32_t a, p;
	uint32_t m, k;
	int i, j;

	withwriter = v != NULL;
	fresh();
	for (i = 0; i < (int)nelem(voices); i++) {
		copyvoice(voices[i], sizes[i]);
		snprintf(what, sizeof what, "InitFileNar(%s)", voices[i]);
		a = acallname(as.openread, voices[i]);
		p = OpenRead(voices[i]);
		for (j = 0; j < 4; j++) {
			k = acall(as.read, 3, a, (int32_t)(uintptr_t)(abuf + 1024), 4);
			m = Read(p, pbuf, 4);
			if (k != m || memcmp(abuf + 1024, pbuf, m) != 0)
				fail("read %d: the assembly's %u bytes, ours %u", j, k, m);
		}
		k = acall(as.seek, 3, a, 0, 0);
		m = Seek(p, 0, 0);
		if (k != m)
			fail("the assembly at %u, ours %u", k, m);
		k = acall(as.read, 3, a, (int32_t)(uintptr_t)(abuf + 1024), 40000);
		m = Read(p, pbuf, 40000);
		if (k != m || memcmp(abuf + 1024, pbuf, m) != 0)
			fail("the assembly's %u bytes, ours %u", k, m);
		acall(as.close, 1, a);
		Close(p);
	}
	for (i = 0; i < 6; i++) {
		snprintf(name, sizeof name, "S%u.LBA", rnd() % 10000);
		snprintf(what, sizeof what, "SaveGame(%s)", name);
		a = acallname(as.openwrite, name);
		p = OpenWrite(name);
		if (a == 0 || p == 0)
			fail("the assembly's handle %d, ours %d", a, p);
		for (j = 0; j < 24; j++) {
			m = 1 + rnd() % 300;
			for (k = 0; k < m; k++)
				abuf[1024 + k] = pbuf[k] = rnd();
			acall(as.write, 3, a, (int32_t)(uintptr_t)(abuf + 1024), (int32_t)m);
			Write(p, pbuf, m);
		}
		acall(as.close, 1, a);
		Close(p);
		samesearch("*.LBA");
		if (i % 2 == 1) {
			snprintf(what, sizeof what, "Delete(%s)", name);
			if (acallname(as.delete, name) != Delete(name))
				fail("the deletes differ");
		}
	}
	snprintf(what, sizeof what, "lba.cfg written again");
	m = FileSize("LBA.CFG");
	a = acallname(as.openwrite, "LBA.CFG");
	p = OpenWrite("LBA.CFG");
	for (k = 0; k < m + 17; k++)
		abuf[1024 + k] = pbuf[k] = rnd();
	acall(as.write, 3, a, (int32_t)(uintptr_t)(abuf + 1024), (int32_t)(m + 17));
	Write(p, pbuf, m + 17);
	acall(as.close, 1, a);
	Close(p);
	if (withwriter)
		endwrites();
	samefiles();
	printf("game%s: %s\n", withwriter ? ", a writer" : "", nfail == 0 ? "the same" : "differ");
}

static void
addext(void *v)
{
	static char *paths[][2] = {
		{"VOX\\EN_000", ".VOX"}, {"VOX\\EN_GAM.VOX", ".ord"}, {"FLA\\DRAGON3", ".FLA"},
		{"\\LBA\\VOX\\EN_SYS", ".VOX"}, {"D:\\LBA\\FLA\\BAFFE", ".FLA"}, {"SAVE", "LBA"},
		{"S1234.LBA", ".BAK"}, {"LBA.CFG", ""}, {"A.B.C", ".D"}, {"NAME", ".EXTX"},
	};
	char p[300];
	int i;

	USED(v);
	for (i = 0; i < (int)nelem(paths); i++) {
		snprintf(what, sizeof what, "AddExt(%s, %s)", paths[i][0], paths[i][1]);
		strcpy((char*)abuf, paths[i][0]);
		strcpy((char*)abuf + 300, paths[i][1]);
		acall(as.addext, 2, (int32_t)(uintptr_t)abuf, (int32_t)(uintptr_t)(abuf + 300));
		strcpy(p, paths[i][0]);
		AddExt(p, paths[i][1]);
		if (strcmp((char*)abuf, p) != 0)
			fail("the assembly's %s, ours %s", (char*)abuf, p);
	}
	printf("addext: %d paths, %s\n", i, nfail == 0 ? "the same" : "differ");
}

static void
convention(void *v)
{
	USED(v);
	fresh();
	snprintf(what, sizeof what, "Touch(S1.LBA)");
	acallname(as.touch, "S1.LBA");
	printf("convention: %s\n", nfail == 0 ? "as it should" : "fails");
}

int
main(int argc, char **argv)
{
	static struct
	{
		char	*name;
		void	(*fn)(void*);
	} parts[] = {
		{"random", atrandom},
		{"game", game},
		{"random, a writer", atrandom},
		{"game, a writer", game},
		{"addext", addext},
		{"convention", convention},
	};
	int i, nbad;

	oracle = "fileoracle";
	if (argc != 5 || strcmp(argv[1], "-d") != 0) {
		fprintf(stderr, "usage: fileoracle -d dir lba0.elf lba0.sym\n");
		return 2;
	}
	dir = argv[2];
	if (mkdir(dir, 0777) < 0 && access(dir, W_OK) < 0)
		fatal("%s: cannot make it", dir);
	snprintf(dosdir, sizeof dosdir, "%s/dos", dir);
	snprintf(gamedir, sizeof gamedir, "%s/game", dir);
	snprintf(prefdir, sizeof prefdir, "%s/pref", dir);
	pcload(argv[3], argv[4]);
	as.openread = lookup("OpenRead");
	as.openwrite = lookup("OpenWrite");
	as.read = lookup("Read");
	as.write = lookup("Write");
	as.close = lookup("Close");
	as.seek = lookup("Seek");
	as.delete = lookup("Delete");
	as.filesize = lookup("FileSize");
	as.addext = lookup("AddExt");
	as.touch = lookup("Touch");
	as.findfirst = lookup("_dos_findfirst");
	as.findnext = lookup("_dos_findnext");
	abuf = malloc(Maxsize + 4096);
	pbuf = malloc(Maxsize + 4096);
	if (abuf == NULL || pbuf == NULL)
		fatal("out of memory");
	pcint(0x21, dosfs);
	nbad = 0;
	for (i = 0; i < (int)nelem(parts); i++)
		nbad += inchild(parts[i].name, 600, parts[i].fn,
			strstr(parts[i].name, "writer") != NULL ? parts : NULL);
	if (nbad > 0) {
		fprintf(stderr, "fileoracle: %d failed\n", nbad);
		return 1;
	}
	printf("fileoracle: ok\n");
	return 0;
}
