#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/utime.h>
#else
#include <unistd.h>
#include <utime.h>
#endif
#include "../sys/dat.h"
#include "../sys/dos.h"
#include "../sys/fns.h"

#ifdef _WIN32
#define dup	_dup
#define dup2	_dup2
#define close	_close
#define fileno	_fileno
#define rmdir	_rmdir
#endif

/*
 * filestest: the game's files as sys/files.c gives them, held to what
 * each call must do, those the game writes in the preferences held in
 * memory while open and put on the disk by a writer.
 *
 *	filestest [-s seed] dir
 *
 * Each case runs in a directory of its own under dir, with preferences,
 * two game directories, read in turn, and a CD, three ways. First each
 * file is written at once by its caller, as cfgtest and a run whose
 * writer thread failed write them. Then a writer's jobs run one at a
 * time when the test chooses, the disk changing only when one runs.
 * Then, in four rounds, the jobs run at random, from seed (the time's
 * by default), which is printed. A check of the disk runs the jobs
 * waiting first, and every run of a case leaves the same files.
 *
 * A file is read from the preferences, else from the first game
 * directory that holds it, whatever the case of its name, and the
 * configuration file past them from the program's directory, then from
 * the program's text. A file written goes to the preferences, the
 * directories leading to it made, and one opened to read and write is
 * the game's copied there. The game reads what it wrote at once, a
 * queued job dropped for a later one on its file. Delete removes the
 * preferences' file, renamefile moves it. A write the disk refuses is
 * printed and keeps its bytes for the run, a rename whose write fails
 * keeps its source on the disk, and a run's private files are removed
 * as the root is first read. LBA2's WriteSaveGame, Def_WriteValue's
 * writes and the save manager's copy run as their own code runs them. A
 * file in memory fills a hole with zeros, refuses the reads and writes
 * its open leaves out, seeks before 0 and past 2^31-1 and writes past
 * 64 MB, and the port's handles stop at 32, LoadMalloc giving its block
 * back when none is left. _dos_findfirst lists the preferences' and the
 * game's files as one, sorted whatever the case, however many: in the
 * root each name in its own case, of 245 characters at most, but the
 * private names, and below it 8.3 names in upper case, each dated as
 * its file within DOS's dates. Read only preferences refuse every
 * write. A file whose host's path runs past the port's 1024 bytes is
 * read from the next game directory that holds it, never under its name
 * cut to fit, and a write of it is refused, and so is a program's
 * directory too long. A name kept with ISO 9660's last dot, CD_LBA., is
 * read as CD_LBA where the name itself is missing. The CD, D:, is read
 * alone: a write, a deletion, a rename or a date on its drive refused.
 * A game path is folded as DOS folds it, an empty part and . dropped,
 * .. taking the part before it away, the root staying the root, and one
 * longer than the port's 1024 bytes is refused.
 * Each failure is a line, and filestest exits 1 after any.
 */

enum
{
	Npath = 1024,
	Nmany = 1100,	/* past the found list's first 512 entries and their double */
	Rounds = 4,
	Maxfile = 64<<20,	/* sys/files.c's */
	Nport = 32,	/* sys/files.c's: the handles open at once */
	Nroot = 245,	/* sys/files.c's: the root's longest name the search gives */
};

enum
{
	Mnone,		/* each file written at once by its caller */
	Mstep,		/* the jobs run one at a time when the test says */
	Mrandom,	/* the jobs run at random */
};

typedef struct Dfile Dfile;
typedef struct Disk Disk;

/* A file of the disk, named by its path below the directory read. */
struct Dfile
{
	char	name[Npath];
	uint8_t	*b;
	uint32_t	len;
};

/* The files of a directory and of its directories. */
struct Disk
{
	Dfile	*f;
	int	n;
	int	max;
};

/* a file's time, and the year since 1980 its DOS date must hold */
static struct
{
	char		*name;
	long long	t;	/* seconds since 1970, UTC */
	int		held;	/* by every host's time_t, utime and stat */
	int		year;
} datecases[] = {
	{"Y1969.HQR", -17236800, 0, 0},	/* 1969-06-15 12:00 */
	{"Y1975.HQR", 172065600, 1, 0},	/* 1975-06-15 12:00 */
	{"Y2026.HQR", 1781524800, 1, 46},	/* 2026-06-15 12:00 */
	{"Y2110.HQR", 4432276800, 0, 127},	/* 2110-06-15 12:00 */
};

int32_t	OpenReadWrite(char*);
int32_t	Delete(char*);
void*	LoadMalloc(char*);
void	Touch(char*);
void	AddExt(char*, char*);

static char	cdhome[] = "cd/LBA";	/* the CD's directory of the game */
static char	top[Npath];	/* the run's directory */
static char	root[Npath];	/* the case's directory */
static char	*runname;
static int	casenum;
static int	failed;
static int	mode;
static uint32_t	state;	/* rnd's */
static Disk	last;	/* the preferences as the last job left them */
static int	saved;	/* stderr, while a case's is captured */
static FILE	*capf;
#ifndef _WIN32
static char	readonly[Npath];	/* preferences made read only, made writable again at exit */
#endif

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "filestest: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(2);
}

static void
fail(const char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "filestest: %s, case %d: ", runname, casenum);
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	failed = 1;
}

/* xorshift32 over state, kept nonzero */
static uint32_t
rnd(void)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

/* The host's path of name under the case's directory. */
static char*
host(const char *name)
{
	static char buf[4][Npath];
	static int i;

	i = (i + 1) % 4;
	if (snprintf(buf[i], sizeof buf[i], "%s/%s", root, name) >= (int)sizeof buf[i])
		fatal("%s/%s: too long", root, name);
	return buf[i];
}

#ifdef _WIN32
static void
mkdirs(const char *path)
{
	char p[Npath], *s;

	snprintf(p, sizeof p, "%s", path);
	for (s = p + 1; *s != '\0'; s++)
		if (*s == '/') {
			*s = '\0';
			_mkdir(p);
			*s = '/';
		}
	_mkdir(p);
}
#else
static void
mkdirs(const char *path)
{
	char p[Npath], *s;

	snprintf(p, sizeof p, "%s", path);
	for (s = p + 1; *s != '\0'; s++)
		if (*s == '/') {
			*s = '\0';
			mkdir(p, 0777);
			*s = '/';
		}
	mkdir(p, 0777);
}
#endif

/* Whether the host's path names a directory. */
static int
hostdir(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void
cleardisk(Disk *d)
{
	int i;

	for (i = 0; i < d->n; i++)
		free(d->f[i].b);
	d->n = 0;
}

/* The file name of d, NULL where it lacks it. */
static Dfile*
diskfile(Disk *d, const char *name)
{
	int i;

	for (i = 0; i < d->n; i++)
		if (strcmp(d->f[i].name, name) == 0)
			return &d->f[i];
	return NULL;
}

/* d's file name set to the n bytes of b, added where d lacks it. */
static void
setfile(Disk *d, const char *name, const void *b, uint32_t n)
{
	Dfile *f;

	f = diskfile(d, name);
	if (f == NULL) {
		if (d->n == d->max) {
			d->max = d->max > 0 ? 2*d->max : 64;
			d->f = realloc(d->f, (size_t)d->max * sizeof d->f[0]);
			if (d->f == NULL)
				fatal("no memory");
		}
		f = &d->f[d->n++];
		snprintf(f->name, sizeof f->name, "%s", name);
	} else
		free(f->b);
	f->b = malloc(n > 0 ? n : 1);
	if (f->b == NULL)
		fatal("no memory");
	if (n > 0)
		memcpy(f->b, b, n);
	f->len = n;
}

/* The files of dir and of its directories into d, each named by its path below the first with slashes. */
static void
readtree(const char *dir, const char *below, Disk *d)
{
	char path[Npath], name[Npath];
	struct stat st;
	struct dirent *e;
	uint8_t *b;
	DIR *dp;
	FILE *f;

	dp = opendir(dir);
	if (dp == NULL)
		return;
	while ((e = readdir(dp)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path
		|| snprintf(name, sizeof name, "%s%s", below, e->d_name) >= (int)sizeof name - 1)
			fatal("%s/%s: too long", dir, e->d_name);
		if (stat(path, &st) < 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			strcat(name, "/");
			readtree(path, name, d);
			continue;
		}
		b = malloc(st.st_size > 0 ? (size_t)st.st_size : 1);
		f = fopen(path, "rb");
		if (b == NULL || f == NULL || (st.st_size > 0 && fread(b, 1, (size_t)st.st_size, f) != (size_t)st.st_size))
			fatal("cannot read %s", path);
		fclose(f);
		setfile(d, name, b, (uint32_t)st.st_size);
		free(b);
	}
	closedir(dp);
}

/* Whether a holds b's files, with the same bytes, why saying where they part. */
static int
samedisk(Disk *a, Disk *b, char *why, size_t n)
{
	Dfile *f;
	int i;

	for (i = 0; i < a->n; i++) {
		f = diskfile(b, a->f[i].name);
		if (f == NULL) {
			snprintf(why, n, "%s in the one alone", a->f[i].name);
			return 0;
		}
		if (f->len != a->f[i].len || memcmp(f->b, a->f[i].b, f->len) != 0) {
			snprintf(why, n, "%s of %lu and %lu bytes apart", f->name,
				(unsigned long)a->f[i].len, (unsigned long)f->len);
			return 0;
		}
	}
	if (a->n != b->n) {
		snprintf(why, n, "%d files and %d", a->n, b->n);
		return 0;
	}
	return 1;
}

/* The preferences as the disk holds them, taken as the last job left them. */
static void
snap(void)
{
	cleardisk(&last);
	readtree(host("pref"), "", &last);
}

/* With the jobs run when the test says, the preferences as the last job left them. */
static void
same(void)
{
	char why[2*Npath];
	Disk d;

	memset(&d, 0, sizeof d);
	readtree(host("pref"), "", &d);
	if (!samedisk(&d, &last, why, sizeof why))
		fail("the disk changed with no job run: %s", why);
	cleardisk(&d);
	free(d.f);
}

/* After a call that writes: with the jobs run when the test says, the disk as the last job left it, and with the jobs at random, some run. */
static void
after(void)
{
	int n;

	switch (mode) {
	case Mnone:
		break;
	case Mstep:
		same();
		break;
	case Mrandom:
		for (n = rnd()%3; n > 0 && stepwrite(); n--)
			;
		break;
	}
}

/* The jobs waiting run, and the disk then taken as the last job left it: how many ran. */
static int
settle(void)
{
	int n;

	n = 0;
	if (mode != Mnone)
		while (stepwrite())
			n++;
	snap();
	return n;
}

/*
 * The writer's lock, for files.c, the test standing in for its thread:
 * a wait runs the next job, and with the jobs run at random, so may the
 * taking of the lock, as the thread would finish one between two calls.
 */
static void
hook(int op)
{
	if (op == Wwait && !stepwrite())
		fatal("a wait with no job to run");
	if (op == Wlock && mode == Mrandom && rnd()%4 == 0)
		stepwrite();
}

/* The case's stderr kept, to see what files.c prints. */
static void
capture(void)
{
	fflush(stderr);
	capf = fopen(host("stderr"), "w+");
	if (capf == NULL)
		fatal("cannot write %s", host("stderr"));
	saved = dup(2);
	if (saved < 0 || dup2(fileno(capf), 2) < 0)
		fatal("cannot capture stderr");
}

/* stderr given back, the lines kept printed but those starting with want: how many did. */
static int
captured(char *want)
{
	char line[512];
	int n;

	fflush(stderr);
	dup2(saved, 2);
	close(saved);
	rewind(capf);
	n = 0;
	while (fgets(line, sizeof line, capf) != NULL)
		if (strncmp(line, want, strlen(want)) == 0)
			n++;
		else
			fputs(line, stderr);
	fclose(capf);
	return n;
}

/* The host's file name under the case's directory written with s, a file of the preferences taken as on the disk. */
static void
put(const char *name, const char *s)
{
	FILE *f;
	char *p, *slash;

	p = host(name);
	slash = strrchr(p, '/');
	*slash = '\0';
	mkdirs(p);
	*slash = '/';
	if ((f = fopen(p, "wb")) == NULL || fputs(s, f) == EOF || fclose(f) != 0)
		fatal("cannot write %s", p);
	if (strncmp(name, "pref/", 5) == 0)
		setfile(&last, name + 5, s, (uint32_t)strlen(s));
}

/* The host's file name under the case's directory, read into buf: its bytes, -1 where it is missing. */
static int
get(const char *name, char *buf, size_t n)
{
	FILE *f;
	size_t k;

	if ((f = fopen(host(name), "rb")) == NULL)
		return -1;
	k = fread(buf, 1, n - 1, f);
	buf[k] = '\0';
	fclose(f);
	return (int)k;
}

/* Whether the host's file under the case's directory holds s, the jobs waiting run first. */
static void
holds(const char *name, const char *s)
{
	char buf[256];

	settle();
	if (get(name, buf, sizeof buf) < 0)
		fail("%s: missing, want \"%s\"", name, s);
	else if (strcmp(buf, s) != 0)
		fail("%s: \"%s\", want \"%s\"", name, buf, s);
}

/* Whether the host's file under the case's directory is missing, the jobs waiting run first. */
static void
missing(const char *name)
{
	char buf[256];

	settle();
	if (get(name, buf, sizeof buf) >= 0)
		fail("%s: holds \"%s\", want none", name, buf);
}

/* The game's file name read whole through the port, into buf: -1 where it opens none. */
static int
readall(const char *name, char *buf, size_t n)
{
	uint32_t k;
	int32_t h;

	if ((h = OpenRead((char*)name)) == 0)
		return -1;
	k = Read(h, buf, (uint32_t)(n - 1));
	buf[k] = '\0';
	Close(h);
	return (int)k;
}

/* Whether the game reads s from name. */
static void
reads(const char *name, const char *s)
{
	char buf[256];

	if (readall(name, buf, sizeof buf) < 0)
		fail("game's %s: opens none, want \"%s\"", name, s);
	else if (strcmp(buf, s) != 0)
		fail("game's %s: \"%s\", want \"%s\"", name, buf, s);
}

/* Whether FileSize gives name's size as n. */
static void
sizes(const char *name, uint32_t n)
{
	uint32_t k;

	if ((k = FileSize((char*)name)) != n)
		fail("game's %s: FileSize %lu, want %lu", name, (unsigned long)k, (unsigned long)n);
}

/* The game's file name written whole with s through OpenWrite, as the game writes a save. */
static void
writeall(const char *name, const char *s)
{
	uint32_t n;
	int32_t h;

	if ((h = OpenWrite((char*)name)) == 0) {
		fail("game's %s: cannot open to write", name);
		return;
	}
	n = (uint32_t)strlen(s);
	if (Write(h, (void*)s, n) != n)
		fail("game's %s: short write", name);
	Close(h);
	after();
}

/* The game's file name deleted, as want says it must be: 1, or 0 for none. */
static void
removes(const char *name, int want)
{
	int r;

	r = Delete((char*)name);
	if (r != want)
		fail("game's %s: Delete gives %d, want %d", name, r, want);
	after();
}

/* The game's file from renamed to, as want says it must be: 1, or 0 for none. */
static void
renames(const char *from, const char *to, int want)
{
	int r;

	r = renamefile((char*)from, (char*)to);
	if (r != want)
		fail("game's %s: renamefile to %s gives %d, want %d", from, to, r, want);
	after();
}

/*
 * The names _dos_findfirst lists for pattern and attr, each with its
 * size and dir for a directory, a line each, in buf.
 */
static void
list(const char *pattern, unsigned attr, char *buf, size_t n)
{
	struct find_t ft;
	unsigned r;
	size_t k;

	buf[0] = '\0';
	k = 0;
	for (r = _dos_findfirst(pattern, attr, &ft); r == 0 && k < n; r = _dos_findnext(&ft))
		k += (size_t)snprintf(buf + k, n - k, "%s %lu%s\n", ft.name, (unsigned long)ft.size,
			(ft.attrib & _A_SUBDIR) ? " dir" : "");
}

/* Whether _dos_findfirst lists want for pattern and attr. */
static void
listsattr(const char *pattern, unsigned attr, const char *want)
{
	char buf[2048];

	list(pattern, attr, buf, sizeof buf);
	if (strcmp(buf, want) != 0)
		fail("%s lists \"%s\", want \"%s\"", pattern, buf, want);
}

/* Whether _dos_findfirst lists want for pattern's files. */
static void
lists(const char *pattern, const char *want)
{
	listsattr(pattern, _A_NORMAL, want);
}

/* DOS's date and time of t, as _dos_findfirst gives a file's, its year within 1980 to 2107. */
static void
dosdate(time_t t, int year, unsigned *wdate, unsigned *wtime)
{
	struct tm *tm;

	tm = localtime(&t);
	if (tm == NULL) {
		*wdate = 1<<5 | 1;
		*wtime = 0;
		return;
	}
	if (year < 0)
		year = tm->tm_year < 80 ? 0 : tm->tm_year - 80 > 127 ? 127 : tm->tm_year - 80;
	*wdate = (unsigned)(year<<9 | (tm->tm_mon + 1)<<5 | tm->tm_mday);
	*wtime = (unsigned)(tm->tm_hour<<11 | tm->tm_min<<5 | tm->tm_sec/2);
}

/* Whether Def_ReadValue2 reads key's value in LBA.CFG as want. */
static void
value(char *key, int32_t want)
{
	int32_t v;

	v = 0;
	if (!Def_ReadValue2((unsigned char*)"LBA.CFG", (unsigned char*)key, &v) || v != want)
		fail("LBA.CFG's %s: %ld, want %ld", key, (long)v, (long)want);
}

/* key's value set in LBA.CFG through Def_WriteValue. */
static void
setvalue(char *key, int32_t v)
{
	if (!Def_WriteValue((unsigned char*)"LBA.CFG", (unsigned char*)key, v))
		fail("Def_WriteValue %s: refused", key);
	after();
}

/*
 * LBA.CFG opened for reading and writing, its bytes read and left as
 * they are, which must be want: its close puts the whole file in the
 * preferences, as OpenReadWrite does a game directory's file.
 */
static void
cfgcopy(const char *want)
{
	char buf[256];
	uint32_t n;
	int32_t h;

	if ((h = OpenReadWrite("LBA.CFG")) == 0) {
		fail("OpenReadWrite LBA.CFG: no handle");
		return;
	}
	n = Read(h, buf, sizeof buf - 1);
	buf[n] = '\0';
	if (strcmp(buf, want) != 0)
		fail("OpenReadWrite LBA.CFG: \"%s\", want \"%s\"", buf, want);
	Close(h);
	after();
}

/*
 * The order of the layers, the case of names, and a path below a
 * missing directory or below a file, which names none though the top
 * holds a file of its last part's name, on the disk or in memory. A name
 * in both game directories is the first's, its size too, their voices
 * are listed as one in upper case, and OpenReadWrite seeds the
 * preferences from the last.
 */
static void
layers(void)
{
	char buf[8];
	int32_t h;

	put("pref/X.LBA", "pref's x");
	put("game0/ONE.TXT", "game0's one");
	put("game1/one.txt", "game1's one, longer");
	put("game1/Two.Txt", "game1's two");
	put("pref/two.txt", "pref's two");
	put("game1/THREE.TXT", "game1's three");
	put("game0/VOX/EN_000.VOX", "game0's");
	put("game1/vox/en_000.vox", "game1's voice");
	put("game1/vox/FR_000.VOX", "game1's fr");
	put("game1/S0009.LBA", "game1's save");
	reads("ONE.TXT", "game0's one");
	reads("one.txt", "game0's one");
	sizes("one.txt", 11);
	reads("TWO.TXT", "pref's two");
	reads("three.TXT", "game1's three");
	if (readall("FOUR.TXT", buf, sizeof buf) >= 0)
		fail("FOUR.TXT opens, want none");
	reads("X.LBA", "pref's x");
	writeall("Y.LBA", "game's y");
	if (readall("NODIR\\X.LBA", buf, sizeof buf) >= 0 || readall("NODIR\\Y.LBA", buf, sizeof buf) >= 0)
		fail("NODIR\\X.LBA or NODIR\\Y.LBA opens, want none");
	if (readall("X.LBA\\X.LBA", buf, sizeof buf) >= 0 || readall("Y.LBA\\Y.LBA", buf, sizeof buf) >= 0)
		fail("X.LBA\\X.LBA or Y.LBA\\Y.LBA opens, want none");
	lists("VOX\\*.VOX", "EN_000.VOX 7\nFR_000.VOX 10\n");
	sizes("VOX\\EN_000.VOX", 7);
	sizes("vox\\fr_000.vox", 10);
	if ((h = OpenReadWrite("S0009.LBA")) == 0)
		fail("OpenReadWrite S0009.LBA: no handle");
	else {
		Close(h);
		after();
	}
	reads("S0009.LBA", "game1's save");
	holds("pref/S0009.LBA", "game1's save");
	holds("game1/S0009.LBA", "game1's save");
}

/*
 * A new file, and a file written whole over another: the game reads the
 * last bytes at once, the disk gets them when their job runs, and a job
 * queued for a file is dropped for a later one.
 */
static void
rewrites(void)
{
	int n;

	writeall("NEW.LBA", "first");
	reads("NEW.LBA", "first");
	holds("pref/NEW.LBA", "first");
	writeall("NEW.LBA", "second, longer");
	reads("NEW.LBA", "second, longer");
	lists("*.*", "NEW.LBA 14\n");
	writeall("new.lba", "third");
	reads("NEW.LBA", "third");
	lists("*.*", "NEW.LBA 5\n");
	n = settle();
	if (mode == Mstep && n != 1)
		fail("the rewrites' jobs: %d where 1 was due", n);
	holds("pref/NEW.LBA", "third");
	reads("NEW.LBA", "third");
}

/*
 * Delete, of the preferences' files, with new bytes on their way, of
 * one the game's directory also holds, twice, the game's showing after,
 * and of the game's. A file deleted by another spelling and written
 * again under it takes that spelling, and one written by another
 * spelling keeps its own.
 */
static void
removals(void)
{
	char buf[16];

	put("pref/OLD.LBA", "old");
	put("pref/S0005.LBA", "pref's five");
	put("pref/S0006.LBA", "six");
	put("pref/S0007.LBA", "seven");
	put("game0/KEEP.TXT", "kept");
	put("game0/S0005.LBA", "game0's");
	writeall("OLD.LBA", "new");
	removes("OLD.LBA", 1);
	if (readall("OLD.LBA", buf, sizeof buf) >= 0)
		fail("OLD.LBA opens once deleted");
	lists("OLD.*", "");
	missing("pref/OLD.LBA");
	removes("KEEP.TXT", 0);
	holds("game0/KEEP.TXT", "kept");
	writeall("OLD.LBA", "back");
	reads("OLD.LBA", "back");
	holds("pref/OLD.LBA", "back");
	removes("S0005.LBA", 1);
	reads("S0005.LBA", "game0's");
	sizes("S0005.LBA", 7);
	removes("S0005.LBA", 0);
	reads("S0005.LBA", "game0's");
	removes("s0006.lba", 1);
	writeall("s0006.lba", "six again");
	lists("S0006.*", "s0006.lba 9\n");
	writeall("s0007.lba", "seven again");
	lists("S0007.*", "S0007.LBA 11\n");
	holds("pref/s0006.lba", "six again");
	holds("pref/S0007.LBA", "seven again");
}

/*
 * renamefile: a file renamed over another, which keeps its spelling, a
 * missing one renamed, and one renamed over a directory, which OpenRead,
 * FileSize, OpenWrite and Delete refuse too.
 */
static void
renaming(void)
{
	char buf[8];
	int32_t h;

	put("pref/S0007.LBA", "seven");
	put("pref/S0008.LBA", "eight");
	mkdirs(host("pref/D.LBA"));
	if (renamefile("S0008.LBA", "D.LBA") != 0)
		fail("renamefile over a directory succeeds");
	if ((h = OpenRead("D.LBA")) != 0) {
		fail("OpenRead of a directory opens");
		Close(h);
	}
	sizes("D.LBA", 0);
	if ((h = OpenWrite("D.LBA")) != 0) {
		fail("OpenWrite of a directory opens");
		Close(h);
	}
	if (Delete("D.LBA") != 0)
		fail("Delete of a directory succeeds");
	after();
	renames("S0008.LBA", "s0007.lba", 1);
	reads("S0007.LBA", "eight");
	if (readall("S0008.LBA", buf, sizeof buf) >= 0)
		fail("S0008.LBA opens once renamed");
	lists("S000?.LBA", "S0007.LBA 5\n");
	renames("S0099.LBA", "S0098.LBA", 0);
	holds("pref/S0007.LBA", "eight");
	missing("pref/S0008.LBA");
	missing("pref/S0098.LBA");
}

/*
 * Writes, removals and renames of three saves, the jobs left to wait:
 * with the jobs run when the test says, those a later one on the same
 * file makes needless are dropped, and the disk gets the three left, in
 * order.
 */
static void
order(void)
{
	char buf[16];
	int n;

	writeall("S0011.LBA", "first");
	removes("S0011.LBA", 1);
	writeall("S0011.LBA", "second");
	renames("S0011.LBA", "S0012.LBA", 1);
	reads("S0012.LBA", "second");
	writeall("S0011.LBA", "third");
	removes("S0012.LBA", 1);
	renames("S0011.LBA", "S0013.LBA", 1);
	writeall("S0012.LBA", "fourth");
	if (readall("S0011.LBA", buf, sizeof buf) >= 0)
		fail("S0011.LBA opens once renamed");
	reads("S0012.LBA", "fourth");
	reads("S0013.LBA", "third");
	lists("*.LBA", "S0012.LBA 6\nS0013.LBA 5\n");
	n = settle();
	if (mode == Mstep && n != 3)
		fail("the jobs: %d where 3 were due", n);
	missing("pref/S0011.LBA");
	holds("pref/S0012.LBA", "fourth");
	holds("pref/S0013.LBA", "third");
}

/* The listing: the layers as one, sorted whatever the case, once each, in the root in their own case and below it 8.3 names in upper case. */
static void
listings(void)
{
	put("game1/b.hqr", "bb");
	put("game0/A.HQR", "a");
	put("pref/c.Hqr", "ccc");
	put("game1/A.HQR", "other");
	put("game0/long name.hqr", "x");
	put("game1/FLA/b.fla", "bb");
	put("game0/fla/A.FLA", "a");
	put("pref/Fla/c.Fla", "ccc");
	put("game1/FLA/A.FLA", "other");
	put("game0/fla/long name.fla", "x");
	put("game0/fla/TOOLONGNAME.FLA", "x");
	lists("*.HQR", "A.HQR 1\nb.hqr 2\nc.Hqr 3\nlong name.hqr 1\n");
	lists("*.hqr", "A.HQR 1\nb.hqr 2\nc.Hqr 3\nlong name.hqr 1\n");
	lists("FLA\\*.FLA", "A.FLA 1\nB.FLA 2\nC.FLA 3\n");
	listsattr("*.*", _A_SUBDIR, "A.HQR 1\nb.hqr 2\nc.Hqr 3\nFla 0 dir\nlong name.hqr 1\n");
}

/*
 * A listing of more entries than the found list starts with, through
 * the layers: each listed once, the first layer's, sorted.
 */
static void
many(void)
{
	char name[Npath], want[32];
	struct find_t ft;
	unsigned r;
	int n;

	for (n = 0; n < Nmany; n++) {
		snprintf(name, sizeof name, "%s/F%04d.HQR", n%2 == 0 ? "pref" : "game0", n);
		put(name, "x");
		if (n%10 == 0) {
			snprintf(name, sizeof name, "game1/f%04d.hqr", n);
			put(name, "yy");
		}
	}
	n = 0;
	r = _dos_findfirst("*.HQR", _A_NORMAL, &ft);
	while (r == 0 && n < Nmany) {
		snprintf(want, sizeof want, "F%04d.HQR", n);
		if (strcmp(ft.name, want) != 0 || ft.size != 1) {
			fail("*.HQR lists %s %lu as its entry %d, want %s 1", ft.name,
				(unsigned long)ft.size, n, want);
			return;
		}
		n++;
		r = _dos_findnext(&ft);
	}
	if (r == 0 || n != Nmany)
		fail("*.HQR lists %s%d entries, want %d", r == 0 ? "more than " : "", n, Nmany);
}

/*
 * A file's date and time as DOS's words, its year within DOS's 1980 to
 * 2107, and a time localtime cannot give DOS's first day. A time the
 * host's time_t, utime or stat gives back otherwise, as Wine's stat
 * gives back 1969's and 2110's, is passed by, and fails where
 * datecases marks it held. Every file is dated before the preferences
 * are first read.
 */
static void
dates(void)
{
	char name[Npath];
	struct find_t ft;
	struct utimbuf ut;
	struct stat st;
	time_t t;
	unsigned wdate, wtime;
	int i, dated[nelem(datecases)];

	for (i = 0; i < (int)nelem(datecases); i++) {
		t = (time_t)datecases[i].t;
		snprintf(name, sizeof name, "pref/%s", datecases[i].name);
		put(name, "x");
		ut.actime = t;
		ut.modtime = t;
		dated[i] = (long long)t == datecases[i].t && utime(host(name), &ut) == 0
			&& stat(host(name), &st) == 0 && st.st_mtime == t;
		if (!dated[i] && datecases[i].held)
			fail("%s: cannot date it %lld", name, datecases[i].t);
	}
	for (i = 0; i < (int)nelem(datecases); i++) {
		if (!dated[i])
			continue;
		if (_dos_findfirst(datecases[i].name, _A_NORMAL, &ft) != 0) {
			fail("%s lists nothing", datecases[i].name);
			continue;
		}
		dosdate((time_t)datecases[i].t, datecases[i].year, &wdate, &wtime);
		if (ft.wr_date != wdate || ft.wr_time != wtime)
			fail("%s: date %#x, time %#x, want %#x, %#x", datecases[i].name, ft.wr_date, ft.wr_time, wdate, wtime);
	}
}

/*
 * WriteSaveGame's sequence (sources/savegame.c): the record into the
 * .TMP, its version byte and tail written through OpenReadWrite, the
 * .TMP renamed over the save.
 */
static void
savegame(char *name, const char *rec, char ver, const char *tail)
{
	char tmp[256];
	uint32_t n;
	int32_t h;

	snprintf(tmp, sizeof tmp, "%s", name);
	AddExt(tmp, "TMP");
	writeall(tmp, rec);
	if ((h = OpenReadWrite(tmp)) == 0) {
		fail("OpenReadWrite %s: no handle", tmp);
		return;
	}
	n = (uint32_t)strlen(tail);
	if (Seek(h, 0, SEEK_SET) != 0 || Write(h, &ver, 1) != 1 || Seek(h, 0, SEEK_END) <= 0
	|| Write(h, (void*)tail, n) != n)
		fail("the version and tail of %s", tmp);
	Close(h);
	after();
	renames(tmp, name, 1);
}

/* The jobs waiting run as settle runs them, the disk never holding name meanwhile with the jobs run when the test says: how many ran. */
static int
settlewithout(const char *name)
{
	char buf[8];
	int n;

	n = 0;
	if (mode != Mnone)
		while (stepwrite()) {
			n++;
			if (mode == Mstep && get(name, buf, sizeof buf) >= 0)
				fail("%s on the disk", name);
		}
	snap();
	return n;
}

/*
 * A save as WriteSaveGame writes it, seen at once and its .TMP gone.
 * With the jobs run when the test says, the .TMP's writes are dropped
 * for its rename, so that the disk holds the save alone. Then saved
 * again with the jobs done and still to be taken back, and twice before
 * they run.
 */
static void
savegames(void)
{
	char buf[8];
	int n;

	savegame("S0101.LBA", "-record one", '1', ", tail one");
	reads("S0101.LBA", "1record one, tail one");
	if (readall("S0101.TMP", buf, sizeof buf) >= 0)
		fail("S0101.TMP opens once renamed");
	n = settlewithout("pref/S0101.TMP");
	if (mode == Mstep && n != 2)
		fail("the save's jobs: %d where 2 were due", n);
	holds("pref/S0101.LBA", "1record one, tail one");
	savegame("S0101.LBA", "-record two", '2', ", tail two");
	settlewithout("pref/S0101.TMP");
	savegame("S0101.LBA", "-record three", '3', ", tail three");
	savegame("S0101.LBA", "-record four", '4', ", four");
	reads("S0101.LBA", "4record four, four");
	lists("S0101.*", "S0101.LBA 18\n");
	n = settlewithout("pref/S0101.TMP");
	if (mode == Mstep && n != 2)
		fail("the two saves' jobs: %d where 2 were due", n);
	holds("pref/S0101.LBA", "4record four, four");
	missing("pref/S0101.TMP");
}

static char cfgtext[] =
	"; the preferences' own\r\n"
	"Language: English\r\n"
	"WaveVolume: 255\r\n"
	"MusicVolume: 187\r\n"
	"CDVolume: 216\r\n"
	"LineVolume: 216\r\n"
	"MasterVolume: 255\r\n"
	"End: 1\r\n";

static char *volumes[] = {"WaveVolume", "MusicVolume", "CDVolume", "LineVolume", "MasterVolume", NULL};

/*
 * The volumes' five keys written one after the other into an lba.cfg
 * spelled in lowercase, each read back by the next: the file keeps its
 * spelling, and with the jobs run when the test says, the disk gets the
 * last write alone.
 */
static void
cfgs(void)
{
	int i, n;

	put("pref/lba.cfg", cfgtext);
	put("game0/LBA.CFG", "Language: French\r\n");
	for (i = 0; volumes[i] != NULL; i++) {
		setvalue(volumes[i], 100+i);
		value(volumes[i], 100+i);
		if (i > 0)
			value(volumes[i-1], 100+i-1);
	}
	lists("*.CFG", "lba.cfg 137\n");
	n = settle();
	if (mode == Mstep && n != 1)
		fail("lba.cfg's jobs: %d where 1 was due", n);
	for (i = 0; volumes[i] != NULL; i++)
		value(volumes[i], 100+i);
	holds("pref/lba.cfg", "; the preferences' own\r\nLanguage: English\r\nWaveVolume: 100\r\nMusicVolume: 101\r\n"
		"CDVolume: 102\r\nLineVolume: 103\r\nMasterVolume: 104\r\nEnd: 1\r\n");
	holds("game0/LBA.CFG", "Language: French\r\n");
}

#ifndef _WIN32
/*
 * A game directory's configuration file linked to nothing: its size 0
 * and its open failing, the lookup stopping at the first directory that
 * names it, before the program's text. On POSIX systems, as Windows'
 * links need a privilege.
 */
static void
deadconfig(void)
{
	int32_t h;

	if (symlink("nowhere", host("game0/LBA.CFG")) < 0)
		fatal("cannot link %s: %s", host("game0/LBA.CFG"), strerror(errno));
	sizes("LBA.CFG", 0);
	if ((h = OpenRead("LBA.CFG")) != 0) {
		fail("OpenRead of an LBA.CFG linked to nothing opens");
		Close(h);
	}
	if (remove(host("game0/LBA.CFG")) != 0)
		fatal("cannot remove %s", host("game0/LBA.CFG"));
}
#endif

/*
 * The configuration file's places, as sys/sdl.c and sys/sys.c give
 * them: past the preferences and the game's directories, the program's
 * directory, then the program's text of it. Each answers OpenRead and
 * OpenReadWrite as the places before it lose the file, another name of
 * the root taken from the program's directory never, and Def_WriteValue
 * writes into the preferences the whole file it read, its key changed,
 * the file it read left as it was.
 */
static void
config(void)
{
	static char own[] = "; the game's\r\nLanguage: Deutsch\r\nWaveVolume: 5\r\n";
	static char beside[] = "; beside the program\r\nLanguage: Italiano\r\nWaveVolume: 7\r\n";
	static char text[] = "; the program's\r\nLanguage: English\r\nWaveVolume: 9\r\n";
	char buf[8];

	put("prog/lba.cfg", beside);
	put("prog/S0001.LBA", "the program's");
	put("game1/LBA.CFG", own);
	if (setprogdir(host("prog")) != 0)
		fail("%s refused", host("prog"));
	bindconfig("LBA.CFG", text);
	if (readall("S0001.LBA", buf, sizeof buf) >= 0)
		fail("S0001.LBA, the program's directory's, opens");
	value("WaveVolume", 5);
	cfgcopy(own);
	holds("pref/LBA.CFG", own);
	removes("LBA.CFG", 1);
	setvalue("WaveVolume", 41);
	value("WaveVolume", 41);
	holds("pref/LBA.CFG", "; the game's\r\nLanguage: Deutsch\r\nWaveVolume: 41\r\n");
	holds("game1/LBA.CFG", own);
	removes("LBA.CFG", 1);
	value("WaveVolume", 5);
	if (remove(host("game1/LBA.CFG")) != 0)
		fatal("cannot remove %s", host("game1/LBA.CFG"));
	value("WaveVolume", 7);
	cfgcopy(beside);
	removes("LBA.CFG", 1);
	setvalue("WaveVolume", 43);
	holds("pref/LBA.CFG", "; beside the program\r\nLanguage: Italiano\r\nWaveVolume: 43\r\n");
	holds("prog/lba.cfg", beside);
	removes("lba.cfg", 1);
	if (remove(host("prog/lba.cfg")) != 0)
		fatal("cannot remove %s", host("prog/lba.cfg"));
#ifndef _WIN32
	deadconfig();
#endif
	value("WaveVolume", 9);
	cfgcopy(text);
	removes("LBA.CFG", 1);
	setvalue("WaveVolume", 47);
	settle();
	value("WaveVolume", 47);
	holds("pref/LBA.CFG", "; the program's\r\nLanguage: English\r\nWaveVolume: 47\r\n");
	setprogdir("");
}

/*
 * The 1994 save manager's copy (sources/gamemenu.c): a free name sought
 * by FileSize past a save on the disk, one just written, one of the
 * game's directory and one just removed, then the save read whole and
 * written under it with another player's name.
 */
static void
copysave(void)
{
	static uint8_t save[] = "\003Twinsen\0the rest of the save";
	static uint8_t want[] = "\003Zoe\0the rest of the save";
	char name[16], buf[64];
	uint32_t size, n;
	uint8_t *p;
	int32_t h;
	int i, k;
	FILE *f;

	if ((f = fopen(host("pref/S0001.LBA"), "wb")) == NULL || fwrite(save, 1, sizeof save - 1, f) != sizeof save - 1
	|| fclose(f) != 0)
		fatal("cannot write %s", host("pref/S0001.LBA"));
	setfile(&last, "S0001.LBA", save, sizeof save - 1);
	put("pref/S0004.LBA", "four");
	put("game0/S0003.LBA", "three");
	writeall("S0002.LBA", "two");
	removes("S0004.LBA", 1);
	for (i = 1; i <= 4; i++) {
		snprintf(name, sizeof name, "S%04d.LBA", i);
		if (FileSize(name) == 0)
			break;
	}
	if (i != 4) {
		fail("the free name: S%04d.LBA where S0004.LBA was due", i);
		return;
	}
	p = LoadMalloc("S0001.LBA");
	size = FileSize("S0001.LBA");
	if (p == NULL || size != sizeof save - 1) {
		fail("LoadMalloc S0001.LBA");
		return;
	}
	n = (uint32_t)strlen((char*)p+1) + 1;
	if ((h = OpenWrite(name)) == 0) {
		fail("OpenWrite %s: no handle", name);
		Free(p);
		return;
	}
	Write(h, p, 1);
	Write(h, "Zoe", 4);
	Write(h, p+n+1, size-n-1);
	Close(h);
	Free(p);
	after();
	settle();
	k = readall(name, buf, sizeof buf);
	if (k != (int)sizeof want - 1 || memcmp(buf, want, sizeof want - 1) != 0)
		fail("the copy %s: %d bytes, want the %d of Zoe's save", name, k, (int)sizeof want - 1);
	lists("*.LBA", "S0001.LBA 29\nS0002.LBA 3\nS0003.LBA 5\nS0004.LBA 25\n");
}

/*
 * The CD, read alone: a write, a deletion, a rename and a date on its
 * drive refused, the preferences' file of the same name kept.
 */
static void
cd(void)
{
	struct utimbuf ut;
	struct stat st;
	int32_t h;

	put("cd/LBA/VOICE.VOX", "voice");
	put("cd/README.TXT", "readme");
	put("pref/README.TXT", "pref's readme");
	ut.actime = ut.modtime = 1000;
	if (utime(host("pref/README.TXT"), &ut) != 0)
		fatal("cannot date %s", host("pref/README.TXT"));
	reads("D:\\LBA\\VOICE.VOX", "voice");
	reads("D:\\README.TXT", "readme");
	if ((h = OpenWrite("D:\\LBA\\VOICE.VOX")) != 0) {
		fail("D:\\LBA\\VOICE.VOX opens to write");
		Close(h);
	}
	if ((h = OpenWrite("D:\\NEW.TXT")) != 0) {
		fail("D:\\NEW.TXT opens to write");
		Close(h);
	}
	removes("D:\\README.TXT", 0);
	renames("D:\\README.TXT", "OTHER.TXT", 0);
	renames("README.TXT", "D:\\OTHER.TXT", 0);
	Touch("D:\\README.TXT");
	if (stat(host("pref/README.TXT"), &st) != 0 || st.st_mtime != 1000)
		fail("Touch D:\\README.TXT dates the preferences' README.TXT");
	reads("README.TXT", "pref's readme");
	holds("cd/LBA/VOICE.VOX", "voice");
	holds("cd/README.TXT", "readme");
	holds("pref/README.TXT", "pref's readme");
	missing("cd/NEW.TXT");
	missing("pref/NEW.TXT");
	missing("pref/LBA/VOICE.VOX");
	missing("pref/OTHER.TXT");
}

/*
 * Game paths folded as DOS folds them, so that .. stays within the
 * preferences and the game's directories, a file on its way to the disk
 * is read through a doubled separator and written through one, and a
 * path longer than the port's 1024 bytes is refused.
 */
static void
folds(void)
{
	static char toolong[Npath + 64];
	int32_t h;

	put("OUT.TXT", "outside");
	if ((h = OpenRead("..\\OUT.TXT")) != 0) {
		fail("..\\OUT.TXT opens a file above the game's directories");
		Close(h);
	}
	sizes("..\\OUT.TXT", 0);
	if (havefile("SUB\\..\\..\\OUT.TXT"))
		fail("SUB\\..\\..\\OUT.TXT found above the game's directories");
	put("game0/OUT.TXT", "game0's");
	reads("..\\OUT.TXT", "game0's");
	reads("C:\\SUB\\..\\.\\OUT.TXT", "game0's");
	writeall("SUB\\F.TXT", "f");
	reads("SUB\\\\F.TXT", "f");
	reads("SUB\\.\\F.TXT", "f");
	writeall("..\\NEW.TXT", "new");
	writeall("SUB\\\\G.TXT", "g");
	lists("SUB\\..\\SUB\\*.TXT", "F.TXT 1\nG.TXT 1\n");
	holds("pref/NEW.TXT", "new");
	holds("pref/SUB/F.TXT", "f");
	holds("pref/SUB/G.TXT", "g");
	missing("NEW.TXT");
	holds("OUT.TXT", "outside");
	memset(toolong, 'A', sizeof toolong - 1);
	if (havefile(toolong))
		fail("a path of %d bytes found", (int)strlen(toolong));
	sizes(toolong, 0);
	if ((h = OpenRead(toolong)) != 0) {
		fail("a path of %d bytes opens", (int)strlen(toolong));
		Close(h);
	}
}

/*
 * A write the disk refuses, a directory made where the save goes once
 * the root was read: with a writer the game reads the save, the failure
 * is printed and the save keeps its bytes for the run. Written at once,
 * the open is refused. Written again once the directory is gone, the
 * save reaches the disk.
 */
static void
refused(void)
{
	int32_t h;
	int n;

	put("pref/A.LBA", "a");
	lists("*.LBA", "A.LBA 1\n");
	mkdirs(host("pref/B.LBA"));
	capture();
	h = OpenWrite("B.LBA");
	if (mode == Mnone) {
		if (h != 0) {
			fail("B.LBA over a directory: opens to write");
			Close(h);
		}
	} else if (h == 0)
		fail("B.LBA: cannot open to write");
	else {
		if (Write(h, "bytes", 5) != 5)
			fail("B.LBA: short write");
		Close(h);
		after();
		reads("B.LBA", "bytes");
		settle();
		reads("B.LBA", "bytes");
		lists("*.LBA", "A.LBA 1\nB.LBA 5\n");
	}
	if (rmdir(host("pref/B.LBA")) < 0)
		fatal("cannot remove %s", host("pref/B.LBA"));
	snap();
	writeall("B.LBA", "again");
	n = captured("lba: writing B.LBA: ");
	if (n != (mode == Mnone ? 0 : 1))
		fail("the failure printed %d times", n);
	reads("B.LBA", "again");
	holds("pref/B.LBA", "again");
}

/*
 * A rename whose write fails, the target a directory the view has not
 * seen: the failure printed, and the source kept on the disk, where the
 * next run finds the save. The view has it moved for this run. Without
 * a writer the rename is refused.
 */
static void
renamefail(void)
{
	int n;

	writeall("S0010.LBA", "ten");
	settle();
	mkdirs(host("pref/S0011.LBA"));
	capture();
	renames("S0010.LBA", "S0011.LBA", mode != Mnone);
	while (mode != Mnone && stepwrite())
		;
	endwrites();
	n = captured("lba: writing S0011.LBA: ");
	if (n != (mode == Mnone ? 0 : 1))
		fail("the rename's failure printed %d times", n);
	holds("pref/S0010.LBA", "ten");
	if (rmdir(host("pref/S0011.LBA")) < 0 || remove(host("pref/S0010.LBA")) < 0)
		fatal("cannot clear %s", host("pref"));
	snap();
}

/*
 * The voice cache (sources/message.c), a directory of the preferences:
 * its files written whole as the root's are, found by their path
 * whatever the case of its directory, listed in VOX alone, in upper
 * case as DOS listed them, the game's directory's beside, dated on the
 * disk by Touch once written, and deleted. A write makes the
 * directories leading to its file.
 */
static void
voices(void)
{
	struct utimbuf u;
	struct stat st;

	put("pref/VOX/en_002.vox", "two");
	put("pref/EN_009.VOX", "nine");
	put("game0/vox/fr_000.Vox", "fr");
	writeall("VOX\\EN_000.VOX", "zero");
	writeall("vox\\EN_001.VOX", "one");
	lists("VOX\\*.VOX", "EN_000.VOX 4\nEN_001.VOX 3\nEN_002.VOX 3\nFR_000.VOX 2\n");
	reads("vox\\en_000.vox", "zero");
	writeall("NEW\\DIR\\EN_000.VOX", "new");
	lists("NEW\\DIR\\*.VOX", "EN_000.VOX 3\n");
	settle();
	if (!hostdir(host("pref/NEW/DIR")))
		fail("NEW\\DIR: not on the disk");
	u.actime = u.modtime = 1000;
	if (utime(host("pref/VOX/EN_000.VOX"), &u) < 0)
		fatal("cannot date %s", host("pref/VOX/EN_000.VOX"));
	Touch("VOX\\EN_000.VOX");
	if (stat(host("pref/VOX/EN_000.VOX"), &st) != 0 || st.st_mtime == 1000)
		fail("Touch leaves VOX\\EN_000.VOX's date");
	removes("VOX\\EN_000.VOX", 1);
	removes("vox\\EN_001.VOX", 1);
	lists("VOX\\*.VOX", "EN_002.VOX 3\nFR_000.VOX 2\n");
	holds("pref/VOX/en_002.vox", "two");
	holds("pref/NEW/DIR/EN_000.VOX", "new");
	missing("pref/VOX/EN_000.VOX");
	missing("pref/VOX/EN_001.VOX");
}

/*
 * A root file's handle in memory: a write past its end filling with
 * zeros, seeks refused before 0 and past 2^31-1, writes past 64 MB
 * refused, reads past its end and on a write handle, writes on a read
 * handle, an empty file written, a read of bytes on their way keeping a
 * copy, OpenReadWrite seeded from the game's directory writing even if
 * left as it was, a path asked of bytes on their way waiting for them,
 * a 33rd handle refused, and LoadMalloc giving its block back when no
 * handle is left to open its file.
 */
static void
handles(void)
{
	char b[64], path[Npath], *all;
	int32_t h, k, hs[Nport];
	void *p, *m, *r;
	int i;

	put("game0/G.LBA", "the game's g");
	put("game0/G.HQR", "the game's hqr");
	if ((h = OpenWrite("H.LBA")) == 0) {
		fail("OpenWrite H.LBA: no handle");
		return;
	}
	if (Write(h, "0123456789", 10) != 10)
		fail("H.LBA: a write of 10 bytes");
	if (Seek(h, 20, SEEK_SET) != 20 || Write(h, "abcde", 5) != 5)
		fail("H.LBA: a write past its end");
	if (Seek(h, -1, SEEK_SET) != -1 || Seek(h, 0, SEEK_CUR) != 25)
		fail("H.LBA: a seek before its start taken, or its place lost");
	if (Seek(h, -5, SEEK_END) != 20 || Seek(h, 0, 3) != -1)
		fail("H.LBA: a seek from its end, or of no mode");
	if (Read(h, b, 4) != 0)
		fail("H.LBA, opened to write: a read succeeds");
	if (Seek(h, INT32_MAX, SEEK_SET) != INT32_MAX || Seek(h, 1, SEEK_CUR) != -1)
		fail("H.LBA: a seek to 2^31-1 refused, or one past it taken");
	if (Write(h, "x", 1) != 0)
		fail("H.LBA: a write past 64 MB succeeds");
	if (Seek(h, Maxfile-4, SEEK_SET) != Maxfile-4 || Write(h, "xxxxxxxx", 8) != 0)
		fail("H.LBA: a write across 64 MB succeeds");
	if (Write(h, "x", 0) != 0)
		fail("H.LBA: a write of nothing gives bytes");
	Close(h);
	after();
	if ((h = OpenRead("H.LBA")) == 0) {
		fail("OpenRead H.LBA: no handle");
		return;
	}
	/* -1 reads 16000000 bytes, which the buffer must hold */
	all = malloc(16000000);
	if (all == NULL)
		fatal("no memory");
	if (Read(h, all, 0xFFFFFFFF) != 25 || memcmp(all, "0123456789\0\0\0\0\0\0\0\0\0\0abcde", 25) != 0)
		fail("H.LBA: read to its end, not its 25 bytes with their hole of zeros");
	free(all);
	if (Read(h, b, 1) != 0 || Seek(h, 100, SEEK_SET) != 100 || Read(h, b, 1) != 0)
		fail("H.LBA: a read at its end or past it gives bytes");
	if (Seek(h, 3, SEEK_SET) != 3 || Read(h, b, 4) != 4 || memcmp(b, "3456", 4) != 0)
		fail("H.LBA: a read from 3");
	/* written at once, the handle is the disk's, which writes as the C library's does */
	if (mode != Mnone && Write(h, "x", 1) != 0)
		fail("H.LBA, opened to read: a write succeeds");
	Close(h);
	k = mode != Mnone ? OpenRead("H.LBA") : 0;
	writeall("H.LBA", "the second bytes of H");
	if (mode != Mnone && (k == 0 || Read(k, b, sizeof b) != 25 || memcmp(b, "0123456789", 10) != 0))
		fail("H.LBA: the copy a read took of bytes on their way");
	if (k != 0)
		Close(k);
	writeall("Z.LBA", "");
	sizes("Z.LBA", 0);
	if ((h = OpenReadWrite("G.LBA")) == 0)
		fail("OpenReadWrite G.LBA: no handle");
	else {
		Close(h);
		after();
	}
	if ((h = OpenReadWrite("N.LBA")) != 0) {
		fail("OpenReadWrite of no file opens");
		Close(h);
	}
	if ((h = OpenReadWrite("H.LBA")) == 0 || Seek(h, 2, SEEK_SET) != 2 || Write(h, "ZZ", 2) != 2)
		fail("OpenReadWrite H.LBA: a write at 2");
	if (h != 0) {
		Close(h);
		after();
	}
	reads("H.LBA", "thZZsecond bytes of H");
	writeall("R.LBA", "the bytes of r");
	if (readpath("R.LBA", path, sizeof path) < 0 || get("pref/R.LBA", b, sizeof b) != 14
	|| strcmp(b, "the bytes of r") != 0)
		fail("readpath R.LBA: its bytes not on the disk");
	if (mode == Mstep)
		snap();
	for (i = 0; i < Nport; i++)
		if ((hs[i] = OpenRead(i%2 == 0 ? "H.LBA" : "G.HQR")) == 0)
			fail("handle %d refused", i+1);
	if (OpenRead("H.LBA") != 0 || OpenRead("G.HQR") != 0 || OpenWrite("Y.LBA") != 0 || OpenReadWrite("H.LBA") != 0)
		fail("a 33rd handle opens");
	m = Malloc(30);
	Free(m);
	p = LoadMalloc("H.LBA");
	r = Malloc(30);
	if (p != NULL || r != m)
		fail("LoadMalloc's block kept with no handle left");
	Free(r);
	for (i = 0; i < Nport; i++)
		if (hs[i] != 0)
			Close(hs[i]);
	after();
	holds("pref/H.LBA", "thZZsecond bytes of H");
	holds("pref/G.LBA", "the game's g");
	holds("pref/Z.LBA", "");
	missing("pref/N.LBA");
	missing("pref/Y.LBA");
}

/*
 * A run's start. With a writer, the private files a run ended in a job
 * left are removed as the root is first read, and the root's test of a
 * new file removes its own. Written at once, they stay, and the test
 * removes them.
 */
static void
start(void)
{
	put("pref/S0001.LBA", "one");
	put("pref/.lbaw-S0001.LBA.new", "half");
	reads("S0001.LBA", "one");
	lists("*.*", "S0001.LBA 3\n");
	if (mode == Mnone) {
		holds("pref/.lbaw-S0001.LBA.new", "half");
		remove(host("pref/.lbaw-S0001.LBA.new"));
	} else
		missing("pref/.lbaw-S0001.LBA.new");
	missing("pref/.lbaw-probe.new");
	writeall("S0001.LBA", "one again");
	holds("pref/S0001.LBA", "one again");
}

/*
 * Long names in the root, where DotEmu's saves keep theirs, a date
 * (YYYYMMDDhhmmss.LBA, and a quick save's .LBA_quick.LBA after it): the
 * preferences' and the game's directory's listed by name whatever their
 * length, read whatever their case, written over as a save is, renamed
 * and deleted. Below the root a name fits 8.3, and a writer's private
 * file in the game's directory, which no run removes, stays out of the
 * lists.
 */
static void
longnames(void)
{
	put("pref/20200509161619.LBA", "dated");
	put("pref/20200509161619.LBA_quick.LBA", "quick");
	put("pref/S0001.LBA", "one");
	put("game0/longgamename.LBA", "the game's");
	put("game0/.lbaw-S0002.LBA.new", "half");
	put("pref/VOX/EN_000.VOX", "voice");
	put("pref/VOX/EN_0000000.VOX", "long voice");
	lists("*.LBA", "20200509161619.LBA 5\n20200509161619.LBA_quick.LBA 5\nlonggamename.LBA 10\nS0001.LBA 3\n");
	lists("VOX\\*.VOX", "EN_000.VOX 5\n");
	reads("LONGGAMENAME.LBA", "the game's");
	reads("20200509161619.lba", "dated");
	savegame("20200509161619.LBA_quick.LBA", "-quick again", 'Q', ", tail");
	reads("20200509161619.LBA_quick.LBA", "Qquick again, tail");
	renames("20200509161619.LBA", "S0002.LBA", 1);
	renames("S0002.LBA", "20200509161619.lba", 1);
	removes("20200509161619.LBA_quick.LBA", 1);
	lists("*.LBA", "20200509161619.lba 5\nlonggamename.LBA 10\nS0001.LBA 3\n");
	holds("pref/20200509161619.lba", "dated");
	missing("pref/20200509161619.LBA_quick.LBA");
	missing("pref/20200509161619.LBA_quick.TMP");
}

#ifndef _WIN32
/*
 * The root's longest name, whose private name fills a host's 255 bytes,
 * listed and written back, and a name a character longer and one holding
 * a \ left out of the lists. On POSIX systems, as Windows' paths take
 * 260 bytes whole.
 */
static void
longest(void)
{
	char longname[Nroot+1], over[Nroot+2], path[Npath], want[Npath];

	memset(longname, 'L', Nroot-4);
	memcpy(longname+Nroot-4, ".LBA", 5);
	memset(over, 'O', Nroot+1-4);
	memcpy(over+Nroot+1-4, ".LBA", 5);
	snprintf(path, sizeof path, "pref/%s", longname);
	put(path, "longest");
	snprintf(path, sizeof path, "pref/%s", over);
	put(path, "over");
	put("pref/a\\b.LBA", "a backslash");
	snprintf(want, sizeof want, "%s 7\n", longname);
	lists("*.LBA", want);
	writeall(longname, "written back");
	snprintf(want, sizeof want, "%s 12\n", longname);
	lists("*.LBA", want);
	snprintf(path, sizeof path, "pref/%s", longname);
	holds(path, "written back");
}

/*
 * Read only preferences: OpenWrite refuses a file, as fopen did, and so
 * do OpenReadWrite and Def_WriteValue. The test makes them so on POSIX
 * systems, as a user other than root.
 */
static void
readonlypref(void)
{
	int32_t h;

	if (geteuid() == 0)
		return;
	put("pref/S0001.LBA", "one");
	put("pref/LBA.CFG", cfgtext);
	snprintf(readonly, sizeof readonly, "%s", host("pref"));
	if (chmod(readonly, 0555) < 0)
		fatal("cannot make %s read only", readonly);
	if ((h = OpenWrite("S0001.LBA")) != 0 || (h = OpenWrite("S0002.LBA")) != 0 || (h = OpenReadWrite("S0001.LBA")) != 0) {
		fail("a file opens to write in preferences that take no file");
		Close(h);
	}
	if (Def_WriteValue((unsigned char*)"LBA.CFG", (unsigned char*)"WaveVolume", 1))
		fail("Def_WriteValue in preferences that take no file succeeds");
	after();
	settle();
	if (chmod(readonly, 0755) < 0)
		fatal("cannot make %s writable", readonly);
	readonly[0] = '\0';
	reads("S0001.LBA", "one");
	holds("pref/S0001.LBA", "one");
	holds("pref/LBA.CFG", cfgtext);
}
#endif

/* The preferences a case made read only made writable again, the test ended in that case. */
static void
writable(void)
{
#ifndef _WIN32
	if (readonly[0] != '\0')
		chmod(readonly, 0755);
#endif
}

/*
 * DOS's search: *.* taking a name without an extension, as DOS matches
 * the name and the extension each, below the root the 8.3 names alone,
 * in upper case, directories with _A_SUBDIR alone, and a pattern too
 * long for a path DOS's path not found.
 */
static void
searches(void)
{
	static char toolong[2*Npath];
	struct find_t ft;

	put("game0/NOEXT", "x");
	put("pref/S0001.LBA", "one");
	put("pref/VOX/en_000.vox", "x");
	put("pref/VOX/long voice.vox", "x");
	put("pref/VOX/ABCDEFGHIJ.X", "x");
	mkdirs(host("pref/SUB"));
	lists("*.*", "NOEXT 1\nS0001.LBA 3\n");
	listsattr("*.*", _A_SUBDIR, "NOEXT 1\nS0001.LBA 3\nSUB 0 dir\nVOX 0 dir\n");
	lists("VOX\\*.*", "EN_000.VOX 1\n");
	memset(toolong, 'x', sizeof toolong - 1);
	if (_dos_findfirst(toolong, _A_NORMAL, &ft) != 3)
		fail("a pattern of %d bytes: not DOS's path not found", (int)strlen(toolong));
	put("game0/A.TXT", "a");
	put("game0/AB.TXT", "ab");
	writeall("ABC.TXT", "abc");
	lists("A?.TXT", "A.TXT 1\nAB.TXT 2\n");
	lists("A*C.TXT", "A.TXT 1\nAB.TXT 2\nABC.TXT 3\n");
	lists("?.TXT", "A.TXT 1\n");
}

#ifndef _WIN32
/* A directory below dir whose path has len bytes, made, its path in out, which holds Npath bytes. */
static void
deepdir(char *out, const char *dir, size_t len)
{
	size_t k, part;

	k = strlen(dir);
	if (k + 2 > len || len >= Npath)
		fatal("%s: no room for a directory of %lu bytes", dir, (unsigned long)len);
	memcpy(out, dir, k + 1);
	while (k < len) {
		if (len - k <= 201)
			part = len - k - 1;
		else
			part = len - k - 3 < 200 ? len - k - 3 : 200;
		out[k++] = '/';
		memset(out + k, 'd', part);
		k += part;
		out[k] = '\0';
		mkdirs(out);
	}
}

/* The host's file name in the host's directory dir written with s, its path past Npath bytes if need be. */
static void
putat(const char *dir, const char *name, const char *s)
{
	char path[2*Npath];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	if ((f = fopen(path, "wb")) == NULL || fputs(s, f) == EOF || fclose(f) != 0)
		fatal("cannot write %s", path);
}

/*
 * A game directory at the edge of the port's paths, before game1: its
 * LONG.CFG read while the file's path fits in 1024 bytes, and past that
 * game1's, where the name cut to fit, LONG.CF, or the directory itself
 * would be read. Then preferences of 1018 bytes, where LONGDIR is too
 * long a part and X after it fits: LONGDIR\X read from game0, its X
 * left unread, and a write of it refused. Then preferences of 1010
 * bytes holding A.LBA and a save of 27 characters, whose path runs past
 * the port's 1024 bytes: *.LBA lists A.LBA alone, never the long save
 * with the size of the file its name cut to fit names. Then the
 * program's directory: one too long for the port's paths refused, the
 * one before kept, its configuration file read there while its path
 * fits the port's 1024 bytes, the program's text past that.
 */
static void
longpath(void)
{
	static size_t lens[] = {1014, 1015, 1022, 1023};
	static char toolong[2*Npath];
	char deep[Npath], g0[Npath], g1[Npath], cd[Npath], pref[Npath], *game[2], *cds[1];
	int i;
	int32_t h;

	put("game0/LONGDIR/X", "game0's");
	put("game1/LONG.CFG", "game1's");
	strcpy(pref, host("pref"));
	strcpy(g0, host("game0"));
	strcpy(g1, host("game1"));
	strcpy(cd, host("cd"));
	cds[0] = cd;
	for (i = 0; i < (int)nelem(lens); i++) {
		deepdir(deep, host("deep"), lens[i]);
		putat(deep, "LONG.CFG", "deep");
		putat(deep, "LONG.CF", "cut");
		game[0] = deep;
		game[1] = g1;
		setdirs(pref, game, 2, cds, 1);
		reads("LONG.CFG", lens[i] + strlen("/LONG.CFG") < Npath ? "deep" : "game1's");
	}
	deepdir(deep, host("deeppref"), 1018);
	putat(deep, "X", "pref's");
	game[0] = g0;
	game[1] = g1;
	setdirs(deep, game, 2, cds, 1);
	reads("LONGDIR\\X", "game0's");
	if ((h = OpenWrite("LONGDIR\\X")) != 0) {
		fail("LONGDIR\\X opens to write past the preferences' 1024 bytes");
		Close(h);
	}
	reads("LONGDIR\\X", "game0's");
	deepdir(deep, host("deepsave"), 1010);
	putat(deep, "A.LBA", "a");
	putat(deep, "ABCDEFGHIJKLMNOPQRSTUVW.LBA", "long");
	putat(deep, "ABCDEFGHIJKL", "decoy");
	setdirs(deep, game, 2, cds, 1);
	lists("*.LBA", "A.LBA 1\n");
	reads("A.LBA", "a");
	setdirs(pref, game, 2, cds, 1);
	put("prog/LONG.CFG", "the program's");
	memset(toolong, 'x', sizeof toolong - 1);
	if (setprogdir(host("prog")) != 0)
		fail("%s refused", host("prog"));
	else if (setprogdir(toolong) == 0)
		fail("a program's directory of %d bytes taken", (int)strlen(toolong));
	else {
		bindconfig("LONG.CFG", NULL);
		reads("LONG.CFG", "game1's");
	}
	toolong[1023] = '\0';
	if (setprogdir(toolong) != 0)
		fail("a program's directory of 1023 bytes refused");
	toolong[1023] = 'x';
	toolong[1024] = '\0';
	if (setprogdir(toolong) == 0)
		fail("a program's directory of 1024 bytes taken");
	toolong[1024] = 'x';
	if (remove(host("game1/LONG.CFG")) != 0)
		fatal("cannot remove %s", host("game1/LONG.CFG"));
	for (i = 0; i < (int)nelem(lens); i++) {
		deepdir(deep, host("deepprog"), lens[i]);
		putat(deep, "LONG.CFG", "the program's");
		if (setprogdir(deep) != 0) {
			fail("a program's directory of %lu bytes refused", (unsigned long)lens[i]);
			continue;
		}
		setdirs(pref, game, 2, cds, 1);
		bindconfig("LONG.CFG", "the program's text");
		reads("LONG.CFG", lens[i] + strlen("/LONG.CFG") < Npath ? "the program's" : "the program's text");
	}
	setprogdir("");
}

/*
 * A copy of the CD whose root keeps ISO 9660's last dot, CD_LBA., read
 * as CD_LBA, and a directory holding the name in another case and,
 * made after it, the name with a last dot in eight cases, where the
 * name itself is read whatever order the host lists them in.
 */
static void
lastdot(void)
{
	static char *dots[] = {"CD_LBA.", "cd_lba.", "Cd_lba.", "Cd_Lba.", "cD_LBA.", "CD_lba.", "cd_LBA.", "Cd_LbA."};
	char name[64];
	int i;

	put("cd/CD_LBA.", "dot");
	reads("D:\\CD_LBA", "dot");
	if (!havefile("D:\\CD_LBA"))
		fail("D:\\CD_LBA unfound beside CD_LBA.");
	put("cd/BOTH/cd_lba", "none");
	for (i = 0; i < (int)nelem(dots); i++) {
		snprintf(name, sizeof name, "cd/BOTH/%s", dots[i]);
		put(name, "dot");
	}
	reads("D:\\BOTH\\CD_LBA", "none");
}
#endif

/*
 * Every bind a start can make: -g and -c each given the most times the
 * port takes, then the configuration file beside the program and its
 * text, the last game directory's and the last CD's files read.
 */
static void
binds(void)
{
	char dir[Ngame+Ncd][Npath], name[64], pref[Npath], *game[Ngame], *cds[Ncd];
	int i;

	for (i = 0; i < Ngame+Ncd; i++) {
		snprintf(name, sizeof name, i < Ngame ? "g%d/G%d.TXT" : "c%d/C%d.TXT", i, i);
		put(name, name);
		snprintf(name, sizeof name, i < Ngame ? "g%d" : "c%d", i);
		strcpy(dir[i], host(name));
		if (i < Ngame)
			game[i] = dir[i];
		else
			cds[i-Ngame] = dir[i];
	}
	put("prog/LBA.CFG", "the program's");
	strcpy(pref, host("pref"));
	if (setprogdir(host("prog")) != 0)
		fail("%s refused", host("prog"));
	setdirs(pref, game, Ngame, cds, Ncd);
	bindconfig("LBA.CFG", "the text");
	reads("G7.TXT", "g7/G7.TXT");
	reads("D:\\C15.TXT", "c15/C15.TXT");
	reads("LBA.CFG", "the program's");
	setprogdir("");
}

static void (*cases[])(void) = {
	layers,
	rewrites,
	removals,
	renaming,
	order,
	listings,
	many,
	dates,
	savegames,
	cfgs,
	config,
	copysave,
	cd,
	binds,
	folds,
	refused,
	renamefail,
	voices,
	handles,
	start,
	longnames,
#ifndef _WIN32
	longest,
	readonlypref,
#endif
	searches,
#ifndef _WIN32
	longpath,
	lastdot,
#endif
};

/* Case n run in a directory of its own under the run's, its writer as the mode has it. */
static void
runcase(int n)
{
	char *game[2], *cds[1];
	char pref[Npath], g0[Npath], g1[Npath], cd[Npath];

	casenum = n;
	if (snprintf(root, sizeof root, "%s/case%d", top, n) >= (int)sizeof root)
		fatal("%s: too long", top);
	mkdirs(host("pref"));
	mkdirs(host("game0"));
	mkdirs(host("game1"));
	mkdirs(host(cdhome));
	strcpy(pref, host("pref"));
	strcpy(g0, host("game0"));
	strcpy(g1, host("game1"));
	strcpy(cd, host("cd"));
	game[0] = g0;
	game[1] = g1;
	cds[0] = cd;
	setdirs(pref, game, 2, cds, 1);
	setprogdir("");
	setwriter(mode == Mnone ? NULL : hook);
	snap();
	cases[n]();
	endwrites();
	setwriter(NULL);
	snap();
}

/* Every case run in dir/name, in a mode, the random jobs drawn from s. */
static void
runall(const char *dir, char *name, int m, uint32_t s)
{
	int n;

	runname = name;
	mode = m;
	state = s != 0 ? s : 1;
	if (snprintf(top, sizeof top, "%s/%s", dir, name) >= (int)sizeof top)
		fatal("%s/%s: too long", dir, name);
	for (n = 0; n < (int)nelem(cases); n++)
		runcase(n);
}

/* The preferences each case left in run a and in run b, under dir: the same. */
static void
compare(const char *dir, char *a, char *b)
{
	char why[2*Npath], pa[Npath], pb[Npath];
	Disk da, db;
	int n;

	runname = b;
	memset(&da, 0, sizeof da);
	memset(&db, 0, sizeof db);
	for (n = 0; n < (int)nelem(cases); n++) {
		casenum = n;
		snprintf(pa, sizeof pa, "%s/%s/case%d/pref", dir, a, n);
		snprintf(pb, sizeof pb, "%s/%s/case%d/pref", dir, b, n);
		readtree(pa, "", &da);
		readtree(pb, "", &db);
		if (!samedisk(&da, &db, why, sizeof why))
			fail("the files left apart from %s's: %s", a, why);
		cleardisk(&da);
		cleardisk(&db);
	}
	free(da.f);
	free(db.f);
}

int
main(int argc, char **argv)
{
	char run[32];
	uint32_t seed;
	int i, r;

	seed = (uint32_t)time(NULL);
	i = 1;
	if (argc > 2 && strcmp(argv[1], "-s") == 0) {
		seed = (uint32_t)strtoul(argv[2], NULL, 10);
		i = 3;
	}
	if (argc - i != 1) {
		fprintf(stderr, "usage: filestest [-s seed] dir\n");
		return 2;
	}
	atexit(writable);
	printf("filestest: seed %lu\n", (unsigned long)seed);
	fflush(stdout);
	runall(argv[i], "none", Mnone, 0);
	runall(argv[i], "step", Mstep, 0);
	for (r = 1; r <= Rounds; r++) {
		snprintf(run, sizeof run, "random%d", r);
		runall(argv[i], run, Mrandom, seed + (uint32_t)r);
	}
	compare(argv[i], "none", "step");
	for (r = 1; r <= Rounds; r++) {
		snprintf(run, sizeof run, "random%d", r);
		compare(argv[i], "none", run);
	}
	if (failed)
		return 1;
	printf("filestest: ok\n");
	return 0;
}
