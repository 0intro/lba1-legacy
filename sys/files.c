#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "dat.h"
#include "dos.h"
#include "fns.h"

/*
 * The game's files, as lib386/lib_sys/files.c and files_a.asm gave them
 * under DOS, over the preferences, where everything written goes
 * (saves, the voice cache, lba.cfg's changes), and over directories
 * bound at the game's paths (bindpath): the game's directories at the
 * root (the CD's LBA directory, a copy of it or another version's
 * install, one or more) and the CD's root at the CD's drive letter, one
 * or more. A relative path is read from the preferences first, then
 * from each bind at its directory or above it in the order bound, a
 * file or a text of the port's bound alone at a path among them
 * (bindfile, bindtext), as the configuration file beside the program
 * and the program's text of it are bound last (bindconfig). A path on
 * the CD's drive is read from the CD's binds. Each part of a path is
 * found whatever its case. Handles are small numbers, 0 meaning
 * failure, as the game keeps some in a WORD.
 *
 * The files the game writes in the preferences (the saves and lba.cfg
 * at the root, the voice cache in VOX) are held in memory while open
 * and go to the disk whole: a job writes a file's bytes to a private
 * name beside it, syncs them and renames that over the file, or removes
 * a file. With a writer (setwriter, its thread sdl.c's), the jobs wait
 * for it in the order asked, and each directory of the preferences is
 * read once, at its first use, into a view that every lookup of it
 * answers from, so that the game sees its writes at once and waits
 * neither on the disk nor on a directory, which a rename holds while
 * ext4 flushes the file's data. Without a writer each job is done at
 * once and the disk answers.
 */

enum
{
	Nfile = 32,
	Nbind = Ngame + Ncd + 2,	/* -g's and -c's, then lba.cfg's file and text */
	Hbase = 5,	/* DOS's first free handle */
	Npath = 1024,
	Nname = 256,
	Maxfile = 64<<20,	/* a file of the preferences held in memory grows to this at most */
	Tries = 20,	/* Windows: an open, rename or remove tried this often */
	Pause = 50,	/* ms between two tries */
};

typedef struct Bind Bind;
typedef struct Place Place;
typedef struct Dirs Dirs;
typedef struct File File;
typedef struct Entry Entry;
typedef struct Job Job;

/*
 * What is bound at a game path: a host directory, the files below the
 * path read from it, or a file alone, the path's own.
 */
struct Bind
{
	int	cd;	/* the path is on the CD's drive */
	char	path[Npath];	/* with slashes, "" for the drive's root */
	char	dir[Npath];	/* the host's directory or file */
	int	file;	/* dir a file bound alone */
	const char	*text;	/* the port's text bound alone, else NULL */
};

/* Where a game path's file lies: on the host, or in the port's text. */
struct Place
{
	char	path[Npath];	/* the host's, text NULL */
	const char	*text;	/* the port's text, else NULL */
	uint32_t	size;	/* its bytes */
};

struct Dirs
{
	char	pref[Npath];
	Bind	bind[Nbind];	/* searched in this order */
	int	nbind;
	int	cdletter;
	char	prog[Npath];	/* the program's directory, which bindconfig reads */
};

/*
 * A handle's kind: free, a file on the disk, or a file of the
 * preferences in memory.
 */
enum
{
	Hfree,
	Hdisk,
	Hmem,
};

struct File
{
	int	kind;
	FILE	*f;	/* Hdisk's */
	uint8_t	*b;	/* Hmem's bytes */
	uint32_t	len;
	uint32_t	room;	/* the bytes b holds room for */
	uint32_t	pos;
	int	rd;	/* opened for reading */
	int	wr;	/* opened for writing */
	int	changed;	/* to be written at its close */
	char	name[Npath];	/* the preferences' path it was opened by, with slashes */
};

/* What the view holds of a name of a directory of the preferences. */
enum
{
	Edisk,		/* a file on the disk */
	Edir,		/* a directory */
	Ebytes,		/* a file whose bytes the disk is still to get */
	Egone,		/* removed */
};

struct Entry
{
	char	name[Nname];	/* as spelled */
	int	state;
	uint8_t	*b;	/* Ebytes' */
	uint32_t	len;	/* a file's size */
	time_t	mtime;
	uint64_t	job;	/* the last job naming it */
	Entry	*sub;	/* Edir's entries */
	int	listed;	/* Edir's read from the disk into sub */
	Entry	*next;
};

/* A job of the writer's, and where it stands. */
enum
{
	Jwrite,		/* the bytes, whole, as the file */
	Jremove,	/* the file removed */
};

enum
{
	Jqueued,
	Jbusy,
	Jdone,
	Jfailed,
};

struct Job
{
	int	op;
	int	state;	/* under the writer's lock */
	int	err;	/* a failure's errno */
	uint64_t	num;
	char	name[Npath];	/* the preferences' path, with slashes */
	char	path[Npath];	/* the file's on the host */
	char	tmp[Npath];	/* the private name beside it a write goes to first */
	uint8_t	*b;	/* Jwrite's, its own copy */
	uint32_t	len;
	uint64_t	after;	/* Jremove's: the write that holds the bytes, whose failure keeps the file */
	Job	*next;
};

static Dirs dirs;
static File files[Nfile];
static Entry top = {.state = Edir};	/* the preferences' own directory, while a writer runs */
static int writable;	/* the preferences take new files */
static Job *jobs;	/* the writer's, in the order asked, under its lock */
static uint64_t njob;	/* the jobs numbered so far */
static int ending;	/* the writer's loop to end, under its lock */
static void (*hook)(int);	/* the writer's lock, NULL to do every job at once */
static char privstart[] = ".lbaw-";	/* a private name's start, and its end */
static char privend[] = ".new";

static void	freeview(Entry*);

static int
isfile(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* setpref: the preferences, where everything written goes, read again at their next use, the binds cleared. */
void
setpref(char *pref)
{
	if (strlen(pref) >= sizeof dirs.pref)
		fatal("%s: too long a path", pref);
	snprintf(dirs.pref, sizeof dirs.pref, "%s", pref);
	dirs.nbind = 0;
	freeview(&top);
	/* D:, the letter InitCDR gives the CD, from the start */
	dirs.cdletter = 'D';
}

/* The preferences, the game's directories bound at the root in turn, and the CDs' roots bound at their drive's in turn. */
void
setdirs(char *pref, char **game, int ngame, char **cd, int ncd)
{
	char err[256];
	int i;

	setpref(pref);
	for (i = 0; i < ngame; i++)
		if (bindpath(0, "", game[i], err, sizeof err) < 0)
			fatal("%s: %s", game[i], err);
	for (i = 0; i < ncd; i++)
		if (bindpath(1, "", cd[i], err, sizeof err) < 0)
			fatal("%s: %s", cd[i], err);
}

/* The next bind, at path on the CD's drive with cd set, of dir: it, or NULL with the reason in err. */
static Bind*
newbind(int cd, char *path, char *dir, char *err, int nerr)
{
	Bind *b;
	char *p;
	int n, m;

	if (dirs.nbind == Nbind) {
		snprintf(err, nerr, "more than %d binds", Nbind);
		return NULL;
	}
	b = &dirs.bind[dirs.nbind];
	memset(b, 0, sizeof *b);
	n = snprintf(b->path, sizeof b->path, "%s", path);
	m = snprintf(b->dir, sizeof b->dir, "%s", dir);
	if (n < 0 || n >= (int)sizeof b->path || m < 0 || m >= (int)sizeof b->dir) {
		snprintf(err, nerr, "too long a path");
		return NULL;
	}
	for (p = b->path; *p != '\0'; p++)
		if (*p == '\\')
			*p = '/';
	b->cd = cd;
	return b;
}

/*
 * bindpath binds dir at path, a game path on the CD's drive with cd
 * set, "" for the drive's root: the files below path are read from dir
 * after the binds made before. 0, or -1 with the reason in err.
 */
int
bindpath(int cd, char *path, char *dir, char *err, int nerr)
{
	Bind *b;

	if ((b = newbind(cd, path, dir, err, nerr)) == NULL)
		return -1;
	dirs.nbind++;
	return 0;
}

/* bindfile binds the host's file at path, which names it: 0, or -1 with the reason in err. */
int
bindfile(int cd, char *path, char *file, char *err, int nerr)
{
	Bind *b;

	if ((b = newbind(cd, path, file, err, nerr)) == NULL)
		return -1;
	b->file = 1;
	dirs.nbind++;
	return 0;
}

/* bindtext binds text, which the caller keeps, at path, which names it: 0, or -1 with the reason in err. */
int
bindtext(int cd, char *path, const char *text, char *err, int nerr)
{
	Bind *b;

	if ((b = newbind(cd, path, "", err, nerr)) == NULL)
		return -1;
	b->file = 1;
	b->text = text;
	dirs.nbind++;
	return 0;
}


/* The CD's drive letter, for paths that name it. */
void
setcdletter(int c)
{
	dirs.cdletter = toupper(c);
}

/*
 * The program's directory, sdl.c's, whose configuration file
 * bindconfig binds after the game's: 0, or -1 for one too long for the
 * port's paths, the directory before kept.
 */
int
setprogdir(const char *dir)
{
	if (strlen(dir) >= sizeof dirs.prog)
		return -1;
	snprintf(dirs.prog, sizeof dirs.prog, "%s", dir);
	return 0;
}

static int
isdir(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

static int
makedir(const char *path)
{
#ifdef _WIN32
	return _mkdir(path);
#else
	return mkdir(path, 0777);
#endif
}

static void
lock(void)
{
	if (hook != NULL)
		hook(Wlock);
}

static void
unlock(void)
{
	if (hook != NULL)
		hook(Wunlock);
}

/* Whether name is a writer's private file. */
static int
isprivate(const char *name)
{
	size_t n, p, e;

	n = strlen(name);
	p = strlen(privstart);
	e = strlen(privend);
	return n > p+e && strncmp(name, privstart, p) == 0 && strcmp(name+n-e, privend) == 0;
}

/* A name into an entry's, which every one fits (ispref). */
static void
copyname(char *to, const char *name)
{
	size_t n;

	n = strlen(name);
	if (n >= Nname)
		n = Nname-1;
	memcpy(to, name, n);
	to[n] = '\0';
}

/* dir/name in out: 0, or -1 if out is too short. */
static int
pathof(char *out, int n, const char *dir, const char *name)
{
	int k;

	k = snprintf(out, n, "%s/%s", dir, name);
	return k >= 0 && k < n ? 0 : -1;
}

/* The private name a write of the host's file path goes to first, beside it: 0, or -1 if out is too short. */
static int
privatename(char *out, int n, const char *path)
{
	const char *p;
	int k;

	p = strrchr(path, '/');
	if (p == NULL)
		return -1;
	k = snprintf(out, n, "%.*s/%s%s%s", (int)(p - path), path, privstart, p+1, privend);
	return k >= 0 && k < n ? 0 : -1;
}

/* The last part of a path with slashes. */
static const char*
lastpart(const char *rel)
{
	const char *p;

	p = strrchr(rel, '/');
	return p != NULL ? p+1 : rel;
}

/* A new entry at the end of the directory d's. */
static Entry*
addentry(Entry *d, const char *name, int state, uint32_t len, time_t mtime)
{
	Entry *e, **l;

	e = malloc(sizeof *e);
	if (e == NULL)
		fatal("no memory for the preferences' files");
	memset(e, 0, sizeof *e);
	copyname(e->name, name);
	e->state = state;
	e->len = len;
	e->mtime = mtime;
	for (l = &d->sub; *l != NULL; l = &(*l)->next)
		;
	*l = e;
	return e;
}

/* Whether the preferences take a new file: a private one made and removed. */
static int
takesfiles(void)
{
	char probe[Npath], path[Npath];
	FILE *f;

	if (pathof(probe, sizeof probe, dirs.pref, "probe") < 0 || privatename(path, sizeof path, probe) < 0)
		return 0;
	f = fopen(path, "wb");
	if (f == NULL)
		return 0;
	fclose(f);
	remove(path);
	return 1;
}

/*
 * The directory d, the host's at path, read into the view, once, before
 * the writer has a job in it: its files and directories as spelled, and
 * the private files of a run ended in a job removed.
 */
static void
listdir(Entry *d, const char *path)
{
	char file[Npath];
	struct stat st;
	struct dirent *e;
	DIR *dir;

	d->listed = 1;
	dir = opendir(path);
	if (dir == NULL)
		return;
	while ((e = readdir(dir)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0 || strlen(e->d_name) >= Nname)
			continue;
		if (pathof(file, sizeof file, path, e->d_name) < 0)
			continue;
		if (isprivate(e->d_name)) {
			remove(file);
			continue;
		}
		if (stat(file, &st) < 0)
			continue;
		addentry(d, e->d_name, S_ISDIR(st.st_mode) ? Edir : Edisk, st.st_size, st.st_mtime);
	}
	closedir(dir);
}

/*
 * Whether the view answers for the preferences: with a writer, their
 * own directory read at its first use, and whether they take a new
 * file, which holds for the run.
 */
static int
keeping(void)
{
	if (hook == NULL)
		return 0;
	if (!top.listed) {
		listdir(&top, dirs.pref);
		writable = takesfiles();
	}
	return 1;
}

/* The directory d's entry for name, its exact spelling first, then whatever its case, the removed ones too with gone set. */
static Entry*
look(Entry *d, const char *name, int gone)
{
	Entry *e;

	for (e = d->sub; e != NULL; e = e->next)
		if ((gone || e->state != Egone) && strcmp(e->name, name) == 0)
			return e;
	for (e = d->sub; e != NULL; e = e->next)
		if ((gone || e->state != Egone) && watcomstricmp(e->name, name) == 0)
			return e;
	return NULL;
}

/*
 * The entry of the directory d a write of name changes: one that exists
 * keeps its spelling, as a file found whatever its case does, else
 * name's is taken.
 */
static Entry*
spell(Entry *d, const char *name)
{
	Entry *e;

	e = look(d, name, 0);
	if (e != NULL)
		return e;
	e = look(d, name, 1);
	if (e == NULL)
		return addentry(d, name, Egone, 0, 0);
	copyname(e->name, name);
	return e;
}

/* The entry below d whose last job is num, or NULL. */
static Entry*
jobentry(Entry *d, uint64_t num)
{
	Entry *e, *f;

	for (e = d->sub; e != NULL; e = e->next) {
		if (e->job == num)
			return e;
		if (e->state == Edir && (f = jobentry(e, num)) != NULL)
			return f;
	}
	return NULL;
}

/* The entries below d freed, d to be read again. */
static void
freeview(Entry *d)
{
	Entry *e;

	while ((e = d->sub) != NULL) {
		d->sub = e->next;
		freeview(e);
		free(e->b);
		free(e);
	}
	d->listed = 0;
}

/* Append to path, a directory of len bytes, /name: 0, or -2 where path's n bytes cannot hold it, path then the directory. */
static int
append(char *path, size_t len, size_t n, const char *name)
{
	int k;

	k = snprintf(path + len, n - len, "/%s", name);
	if (k < 0 || (size_t)k >= n - len) {
		path[len] = '\0';
		return -2;
	}
	return 0;
}

/*
 * Whether the host's entry e is name and a last dot, whatever its case,
 * as ISO 9660 writes a name without an extension (CD_LBA.;1) and some
 * copies of a CD keep it on the host, where DOS and Windows read the
 * name without the dot.
 */
static int
dotted(const char *e, const char *name)
{
	size_t n;

	n = strlen(name);
	return strlen(e) == n + 1 && e[n] == '.' && watcomstrnicmp(e, name, n) == 0;
}

/*
 * The last part of path (name, from byte len on) spelled as the entry
 * found: Windows finds a name whatever its case, and MoveFileExA gives
 * a file it replaces the spelling it is asked for.
 */
static void
respell(char *path, size_t len, size_t n, const char *name)
{
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h;

	h = FindFirstFileA(path, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	FindClose(h);
	if (watcomstricmp(fd.cFileName, name) == 0)
		append(path, len, n, fd.cFileName);
#endif
	USED(path);
	USED(len);
	USED(n);
	USED(name);
}

/*
 * Append to path, a directory, the entry name names whatever its case:
 * the entry itself if it exists, one equal but for case if there is
 * one, else one with a last dot more (dotted), else name as it is. 0 if
 * found, -1 if not, -2 where path's n bytes cannot hold it.
 */
static int
addpart(char *path, size_t n, const char *name)
{
	char dot[Nname];
	DIR *d;
	struct dirent *e;
	size_t len;
	int r;

	len = strlen(path);
	if (append(path, len, n, name) < 0)
		return -2;
	if (exists(path)) {
		respell(path, len, n, name);
		return 0;
	}
	path[len] = '\0';
	r = -1;
	dot[0] = '\0';
	d = opendir(path);
	if (d != NULL) {
		while ((e = readdir(d)) != NULL) {
			if (watcomstricmp(e->d_name, name) == 0) {
				r = append(path, len, n, e->d_name);
				break;
			}
			if (dot[0] == '\0' && dotted(e->d_name, name))
				snprintf(dot, sizeof dot, "%s", e->d_name);
		}
		closedir(d);
	}
	if (r == -1 && dot[0] != '\0')
		r = append(path, len, n, dot);
	if (r == -1)
		append(path, len, n, name);
	return r;
}

/*
 * The host's path in out of rel in the preferences, from the view: each
 * part as its entry spells it, else as it is, each directory read at its
 * first use, and with make each directory leading to rel made and kept.
 * *d gets the view's directory that holds rel's last part and *e that
 * part's entry, each NULL where the view lacks it, the preferences' own
 * directory being rel "". 0 if the file exists, else -1, out holding the
 * path it would have, or nothing for a path its n bytes cannot hold.
 */
static int
walk(const char *rel, char *out, int n, int make, Entry **d, Entry **e)
{
	char buf[Npath], *p, *q;
	Entry *dir, *f;

	*d = NULL;
	*e = NULL;
	out[0] = '\0';
	if (strlen(dirs.pref) >= (size_t)n || strlen(rel) >= sizeof buf)
		return -1;
	snprintf(out, n, "%s", dirs.pref);
	snprintf(buf, sizeof buf, "%s", rel);
	dir = &top;
	f = &top;
	for (p = buf; p != NULL && *p != '\0'; p = q) {
		q = strchr(p, '/');
		if (q != NULL)
			*q++ = '\0';
		if (*p == '\0')
			continue;
		*d = dir;
		f = dir != NULL ? look(dir, p, 0) : NULL;
		if (append(out, strlen(out), n, f != NULL ? f->name : p) < 0) {
			out[0] = '\0';
			*d = NULL;
			return -1;
		}
		if (f == NULL && dir != NULL && make && q != NULL && makedir(out) == 0) {
			f = addentry(dir, p, Edir, 0, time(NULL));
			f->listed = 1;
		}
		if (f != NULL && f->state == Edir && !f->listed)
			listdir(f, out);
		dir = f != NULL && f->state == Edir ? f : NULL;
	}
	*e = f;
	return f != NULL ? 0 : -1;
}

/*
 * The host's path for a game path under base: each part found whatever
 * its case, from the view where base is the preferences while it is
 * kept (walk). With make, the directories leading to it are created. 0
 * if the file exists, else -1, out holding the path it would have, or
 * nothing for a path its n bytes cannot hold, so that it names no other
 * file.
 */
static int
under(const char *base, const char *rel, char *out, size_t n, int make)
{
	char buf[Npath], *p, *q;
	Entry *d, *e;
	int r;

	if (base == dirs.pref && keeping())
		return walk(rel, out, (int)n, make, &d, &e);
	out[0] = '\0';
	if (strlen(base) >= n || strlen(rel) >= sizeof buf)
		return -1;
	snprintf(out, n, "%s", base);
	snprintf(buf, sizeof buf, "%s", rel);
	r = 0;
	for (p = buf; p != NULL && *p != '\0'; p = q) {
		q = strchr(p, '/');
		if (q != NULL)
			*q++ = '\0';
		if (*p == '\0')
			continue;
		r = addpart(out, n, p);
		if (r == -2) {
			out[0] = '\0';
			return -1;
		}
		if (q != NULL && make && !isdir(out))
			makedir(out);
	}
	return r;
}

/* The rest of rel below path, a bind's, compared whatever its case: NULL if rel lies outside it. */
static const char*
below(const char *path, const char *rel)
{
	size_t n;

	n = strlen(path);
	if (n == 0)
		return rel;
	if (watcomstrnicmp(rel, path, n) != 0)
		return NULL;
	if (rel[n] == '/')
		return rel + n + 1;
	if (rel[n] == '\0')
		return rel + n;
	return NULL;
}

/* findhost: the host's path of rel under dir, each part found whatever its case: 0 if the file exists. */
int
findhost(const char *dir, const char *rel, char *out, int n)
{
	return under(dir, rel, out, (size_t)n, 0);
}

/*
 * bindconfig binds the configuration file name after the binds made
 * before it: the file of that name in the program's directory
 * (setprogdir), found whatever its case, then text, the program's own
 * of it, where text is set.
 */
void
bindconfig(char *name, const char *text)
{
	char path[Npath], err[256];

	if (dirs.prog[0] != 0 && findhost(dirs.prog, name, path, sizeof path) == 0 && !isdir(path)
	&& bindfile(0, name, path, err, sizeof err) < 0)
		fatal("%s: %s", path, err);
	if (text != NULL && bindtext(0, name, text, err, sizeof err) < 0)
		fatal("%s: %s", name, err);
}

/* Where rel lies in the first bind, on the CD's drive with cd set, that holds it: 0 if found. */
static int
locate(int cd, char *rel, Place *p)
{
	Bind *b;
	const char *r;

	for (b = dirs.bind; b < dirs.bind + dirs.nbind; b++) {
		if (b->cd != cd || (r = below(b->path, rel)) == NULL)
			continue;
		p->text = NULL;
		if (b->file) {
			if (r[0] != 0)
				continue;
			p->text = b->text;
			if (b->text != NULL) {
				p->size = strlen(b->text);
				return 0;
			}
			if (snprintf(p->path, sizeof p->path, "%s", b->dir) >= (int)sizeof p->path || !isfile(p->path))
				continue;
			return 0;
		}
		if (under(b->dir, r, p->path, sizeof p->path, 0) == 0)
			return 0;
	}
	return -1;
}

/* The host's path of rel in the first bind, on the CD's drive with cd set, that holds it: 0 if it is a host's file. */
static int
inbinds(int cd, char *rel, char *out, int n)
{
	Place p;
	int k;

	if (locate(cd, rel, &p) < 0 || p.text != NULL)
		return -1;
	k = snprintf(out, n, "%s", p.path);
	return k >= 0 && k < n ? 0 : -1;
}

/* The port's text bound at the root's name rel where no place before it holds the file, else NULL. */
static const char*
textat(char *rel)
{
	Place p;

	if (locate(0, rel, &p) < 0)
		return NULL;
	return p.text;
}

/*
 * Split a game path: 1 on the CD's drive, else 0, for another drive's
 * path or a relative one (the caller chooses), -1 for one past rel's n
 * bytes. rel gets the rest with slashes, folded as DOS folds a path: its
 * empty parts and its . dropped, each .. taking the part before it away,
 * the root staying the root.
 */
static int
splitgame(char *path, char *rel, int n)
{
	char buf[Npath], *p, *q, *s;
	size_t len, k;
	int cd;

	cd = 0;
	if (path[0] != 0 && path[1] == ':') {
		cd = toupper((unsigned char)path[0]) == dirs.cdletter;
		path += 2;
	}
	rel[0] = 0;
	if (strlen(path) >= sizeof buf)
		return -1;
	snprintf(buf, sizeof buf, "%s", path);
	len = 0;
	for (p = buf; p != NULL; p = q) {
		q = strpbrk(p, "\\/");
		if (q != NULL)
			*q++ = 0;
		if (*p == 0 || strcmp(p, ".") == 0)
			continue;
		if (strcmp(p, "..") == 0) {
			s = strrchr(rel, '/');
			len = s != NULL ? (size_t)(s - rel) : 0;
			rel[len] = 0;
			continue;
		}
		k = strlen(p);
		if (len + k + 2 > (size_t)n) {
			rel[0] = 0;
			return -1;
		}
		if (len > 0)
			rel[len++] = '/';
		memcpy(rel + len, p, k + 1);
		len += k;
	}
	return cd;
}

/*
 * Whether a game path names a file of the preferences, which the view
 * holds: rel gets its path, each part of which a host's file name holds.
 */
static int
ispref(char *path, char *rel, int n)
{
	char *p, *q;

	if (splitgame(path, rel, n) != 0 || rel[0] == '\0')
		return 0;
	for (p = rel; p != NULL; p = q) {
		q = strchr(p, '/');
		if (q == p || (q != NULL ? (size_t)(q - p) : strlen(p)) >= Nname)
			return 0;
		if (q != NULL)
			q++;
	}
	return 1;
}

/* A file's bytes synced to the disk: 0, or -1. */
static int
syncfile(FILE *f)
{
#ifdef _WIN32
	return _commit(_fileno(f));
#else
	return fsync(fileno(f));
#endif
}

/*
 * from renamed over to at once: 0, or -1 with errno set, and on Windows
 * _doserrno to its error, as the C library sets them.
 */
static int
moveover(char *from, char *to)
{
#ifdef _WIN32
	unsigned long e;

	if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return 0;
	e = GetLastError();
	_doserrno = e;
	switch (e) {
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
		errno = ENOENT;
		break;
	case ERROR_ACCESS_DENIED:
	case ERROR_SHARING_VIOLATION:
	case ERROR_LOCK_VIOLATION:
		errno = EACCES;
		break;
	case ERROR_DISK_FULL:
	case ERROR_HANDLE_DISK_FULL:
		errno = ENOSPC;
		break;
	case ERROR_WRITE_PROTECT:
		errno = EROFS;
		break;
	case ERROR_NOT_SAME_DEVICE:
		errno = EXDEV;
		break;
	default:
		errno = EIO;
		break;
	}
	return -1;
#else
	return rename(from, to);
#endif
}

/*
 * Whether a failed open, rename or remove is tried again: on Windows,
 * where another program (an antivirus, the indexer) may hold the file,
 * for a second while it fails as such a file does, its access denied or
 * its sharing or lock refused.
 */
static int
again(int i)
{
#ifdef _WIN32
	unsigned long e;

	e = _doserrno;
	_doserrno = 0;
	if (i+1 < Tries && (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION)) {
		Sleep(Pause);
		return 1;
	}
#endif
	USED(i);
	return 0;
}

/* from renamed over to, tried again where again says: 0, or an errno. */
static int
replace(char *from, char *to)
{
	int i, e;

	for (i = 0;; i++) {
		if (moveover(from, to) == 0)
			return 0;
		e = errno;
		if (!again(i))
			return e;
	}
}

/* path removed, one already gone counting as removed: 0, or an errno. */
static int
removefile(char *path)
{
	int i, e;

	for (i = 0;; i++) {
		if (remove(path) == 0 || errno == ENOENT)
			return 0;
		e = errno;
		if (!again(i))
			return e;
	}
}

/*
 * A job's bytes written to its private name, synced, and renamed over
 * its file, so that the file is the old one or the new one whatever ends
 * the program: 0, or an errno.
 */
static int
writefile(Job *j)
{
	FILE *f;
	int i, e;

	for (i = 0; (f = fopen(j->tmp, "wb")) == NULL; i++) {
		e = errno;
		if (!again(i))
			return e;
	}
	e = 0;
	if ((j->len > 0 && fwrite(j->b, 1, j->len, f) != j->len) || fflush(f) != 0 || syncfile(f) != 0)
		e = errno != 0 ? errno : EIO;
	if (fclose(f) != 0 && e == 0)
		e = errno != 0 ? errno : EIO;
	if (e == 0)
		e = replace(j->tmp, j->path);
	if (e != 0)
		remove(j->tmp);
	return e;
}

static int
dojob(Job *j)
{
	static uint64_t failed;
	int e;

	if (j->op == Jremove) {
		if (j->after != 0 && j->after == failed)
			return 0;
		return removefile(j->path);
	}
	e = writefile(j);
	if (e != 0)
		failed = j->num;
	return e;
}

/*
 * The writer's finished jobs taken from its queue, their files settled
 * in the view where each is the last job to name them: a file written
 * goes back to the disk's, a failure is printed and keeps its bytes for
 * the run.
 */
static void
reap(void)
{
	Job *j, *done, **l;
	Entry *e;

	if (hook == NULL)
		return;
	done = NULL;
	lock();
	for (l = &jobs; (j = *l) != NULL;) {
		if (j->state == Jdone || j->state == Jfailed) {
			*l = j->next;
			j->next = done;
			done = j;
		} else
			l = &j->next;
	}
	unlock();
	while ((j = done) != NULL) {
		done = j->next;
		if (j->state == Jfailed)
			fprintf(stderr, "lba: %s %s: %s\n", j->op == Jwrite ? "writing" : "removing",
				j->name, strerror(j->err));
		else {
			e = jobentry(&top, j->num);
			if (e != NULL && e->state == Ebytes) {
				free(e->b);
				e->b = NULL;
				e->state = Edisk;
			}
		}
		free(j->b);
		free(j);
	}
}

/*
 * A job for the writer on the preferences' file rel, the host's at path,
 * its number, the bytes copied: a queued one on the same file dropped,
 * which this one makes needless. The view is the caller's, who reaps
 * before changing it.
 */
static uint64_t
queue(int op, const char *rel, const char *path, uint8_t *b, uint32_t len, uint64_t after)
{
	Job *j, *k, **l;

	j = malloc(sizeof *j);
	if (j == NULL)
		fatal("no memory for the preferences' files");
	memset(j, 0, sizeof *j);
	j->op = op;
	j->state = Jqueued;
	j->num = ++njob;
	j->after = after;
	snprintf(j->name, sizeof j->name, "%s", rel);
	snprintf(j->path, sizeof j->path, "%s", path);
	if (path[0] == '\0' || privatename(j->tmp, sizeof j->tmp, path) < 0) {
		/* a path longer than the host's: failed at once */
		j->state = Jfailed;
		j->err = ENAMETOOLONG;
	}
	if (op == Jwrite) {
		j->b = malloc(len > 0 ? len : 1);
		if (j->b == NULL)
			fatal("no memory for the preferences' files");
		if (len > 0)
			memcpy(j->b, b, len);
		j->len = len;
	}
	lock();
	for (l = &jobs; (k = *l) != NULL;) {
		if (k->state == Jqueued && strcmp(k->path, j->path) == 0) {
			*l = k->next;
			free(k->b);
			free(k);
		} else
			l = &k->next;
	}
	*l = j;
	hook(Wwake);
	unlock();
	return j->num;
}

/* The writer waited for until job num is done. */
static void
waitjob(uint64_t num)
{
	Job *j;

	lock();
	for (;;) {
		for (j = jobs; j != NULL && j->num != num; j = j->next)
			;
		if (j == NULL || j->state == Jdone || j->state == Jfailed)
			break;
		hook(Wwait);
	}
	unlock();
	reap();
}

/* The host's path to read a game path from: 0 if the file exists there. */
int
readpath(char *path, char *out, int n)
{
	char rel[Npath];
	Entry *d, *e;
	int cd;

	out[0] = 0;
	cd = splitgame(path, rel, sizeof rel);
	if (cd < 0)
		return -1;
	if (cd)
		return inbinds(1, rel, out, n);
	/* a file's bytes still on their way: its path waits for them */
	if (ispref(path, rel, sizeof rel) && keeping()) {
		walk(rel, out, n, 0, &d, &e);
		if (e != NULL && e->state == Ebytes)
			waitjob(e->job);
	}
	if (under(dirs.pref, rel, out, n, 0) == 0)
		return 0;
	return inbinds(0, rel, out, n);
}

/* havefile: whether a game path names a file the game can read. */
int
havefile(char *path)
{
	char rel[Npath], out[Npath];
	Place p;
	int cd;

	cd = splitgame(path, rel, sizeof rel);
	if (cd < 0)
		return 0;
	if (cd)
		return locate(1, rel, &p) == 0;
	return readpath(path, out, sizeof out) == 0 || locate(0, rel, &p) == 0;
}

/* havecd: whether the CD's drive has a bind. */
int
havecd(void)
{
	Bind *b;

	for (b = dirs.bind; b < dirs.bind + dirs.nbind; b++)
		if (b->cd)
			return 1;
	return 0;
}

/* A preferences' file's bytes to the disk at once, on the caller's thread: a failure printed. */
static void
writenow(const char *rel, uint8_t *b, uint32_t len)
{
	Job j;
	int e;

	memset(&j, 0, sizeof j);
	snprintf(j.name, sizeof j.name, "%s", rel);
	under(dirs.pref, rel, j.path, sizeof j.path, 1);
	j.b = b;
	j.len = len;
	e = ENAMETOOLONG;
	if (j.path[0] != '\0' && privatename(j.tmp, sizeof j.tmp, j.path) == 0)
		e = writefile(&j);
	if (e != 0)
		fprintf(stderr, "lba: writing %s: %s\n", j.name, strerror(e));
}

/*
 * The preferences' file rel's bytes, b, which it takes: into the view
 * and to the writer, the directories leading to it made, or to the disk
 * at once.
 */
static void
commit(const char *rel, uint8_t *b, uint32_t len)
{
	char path[Npath];
	Entry *d, *e;

	if (!keeping()) {
		writenow(rel, b, len);
		free(b);
		return;
	}
	reap();
	walk(rel, path, sizeof path, 1, &d, &e);
	if (d == NULL) {
		fprintf(stderr, "lba: writing %s: %s\n", rel, strerror(path[0] != '\0' ? ENOENT : ENAMETOOLONG));
		free(b);
		return;
	}
	e = spell(d, lastpart(rel));
	free(e->b);
	e->state = Ebytes;
	e->b = b;
	e->len = len;
	e->mtime = time(NULL);
	e->job = queue(Jwrite, rel, path, b, len, 0);
}

/* A host file's bytes, whole, in memory, and their count: NULL on an error. */
static uint8_t*
slurp(char *path, uint32_t *len)
{
	FILE *f;
	uint8_t *b;
	long n;

	f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	b = NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && n <= Maxfile && fseek(f, 0, SEEK_SET) == 0) {
		b = malloc(n > 0 ? n : 1);
		if (b != NULL && fread(b, 1, n, f) != (size_t)n) {
			free(b);
			b = NULL;
		}
		*len = n;
	}
	fclose(f);
	return b;
}

/* A copy of n bytes, NULL if memory is short. */
static uint8_t*
copybytes(uint8_t *b, uint32_t n)
{
	uint8_t *c;

	c = malloc(n > 0 ? n : 1);
	if (c != NULL && n > 0)
		memcpy(c, b, n);
	return c;
}

static int
freeslot(void)
{
	int i;

	for (i = 0; i < Nfile; i++)
		if (files[i].kind == Hfree)
			return i;
	return -1;
}

static File*
file(int32_t h)
{
	if (h < Hbase || h >= Hbase+Nfile || files[h-Hbase].kind == Hfree)
		return NULL;
	return &files[h-Hbase];
}

static int32_t
diskhandle(FILE *f)
{
	int i;

	i = freeslot();
	if (i < 0) {
		fclose(f);
		return 0;
	}
	memset(&files[i], 0, sizeof files[i]);
	files[i].kind = Hdisk;
	files[i].f = f;
	return Hbase + i;
}

/* A handle on the bytes in memory, b, which it takes, of the preferences' file rel: 0 with every handle taken. */
static int32_t
memhandle(const char *rel, uint8_t *b, uint32_t len, int rd, int wr, int changed)
{
	File *h;
	int i;

	i = freeslot();
	if (i < 0) {
		free(b);
		return 0;
	}
	h = &files[i];
	memset(h, 0, sizeof *h);
	h->kind = Hmem;
	h->b = b;
	h->len = len;
	h->room = len;
	h->rd = rd;
	h->wr = wr;
	h->changed = changed;
	snprintf(h->name, sizeof h->name, "%s", rel);
	return Hbase + i;
}

/* OpenRead is files.c's OpenRead: 0 if the file cannot be opened. */
int32_t
OpenRead(char *name)
{
	char path[Npath], rel[Npath];
	const char *t;
	uint8_t *b;
	Entry *d, *e;
	FILE *f;
	int cd;

	if (ispref(name, rel, sizeof rel) && keeping()) {
		walk(rel, path, sizeof path, 0, &d, &e);
		if (e != NULL && e->state == Ebytes) {
			b = copybytes(e->b, e->len);
			if (b == NULL)
				return 0;
			return memhandle(rel, b, e->len, 1, 0, 0);
		}
	}
	cd = splitgame(name, rel, sizeof rel);
	if (cd < 0)
		return 0;
	if (readpath(name, path, sizeof path) < 0) {
		if (cd || (t = textat(rel)) == NULL)
			return 0;
		b = copybytes((uint8_t*)t, strlen(t));
		if (b == NULL)
			return 0;
		return memhandle(rel, b, strlen(t), 1, 0, 0);
	}
	if (isdir(path))
		return 0;
	f = fopen(path, "rb");
	if (f == NULL)
		return 0;
	return diskhandle(f);
}

/*
 * Whether a preferences' path can take a file: a file's or a free name,
 * the directories leading to it made, in preferences that take new
 * files.
 */
static int
canwrite(const char *rel)
{
	char path[Npath];
	Entry *d, *e;

	if (keeping()) {
		walk(rel, path, sizeof path, 1, &d, &e);
		return writable && d != NULL && (e == NULL || e->state != Edir);
	}
	if (under(dirs.pref, rel, path, sizeof path, 1) == 0 && isdir(path))
		return 0;
	return path[0] != '\0' && takesfiles();
}

/*
 * OpenWrite: a file of the preferences in memory, written whole at its
 * close. 0 for a name outside them: on the CD's drive, empty, or with
 * an empty part or one too long.
 */
int32_t
OpenWrite(char *name)
{
	char rel[Npath];

	if (!ispref(name, rel, sizeof rel) || !canwrite(rel))
		return 0;
	return memhandle(rel, NULL, 0, 0, 1, 1);
}

/* OpenReadWrite works on the preferences' copy in memory, made from the game's. */
int32_t
OpenReadWrite(char *name)
{
	char path[Npath], from[Npath], rel[Npath];
	const char *t;
	uint8_t *b;
	uint32_t len;
	Entry *d, *e;
	int changed;

	if (!ispref(name, rel, sizeof rel) || !canwrite(rel))
		return 0;
	e = NULL;
	if (keeping())
		walk(rel, path, sizeof path, 0, &d, &e);
	changed = 0;
	len = 0;
	if (e != NULL && e->state == Ebytes) {
		b = copybytes(e->b, e->len);
		len = e->len;
	} else if (under(dirs.pref, rel, path, sizeof path, 0) == 0 && !isdir(path))
		b = slurp(path, &len);
	else if (inbinds(0, rel, from, sizeof from) == 0 && !isdir(from)) {
		/* the preferences' copy written even if left as it is */
		b = slurp(from, &len);
		changed = 1;
	} else if ((t = textat(rel)) != NULL) {
		len = strlen(t);
		b = copybytes((uint8_t*)t, len);
		changed = 1;
	} else
		return 0;
	if (b == NULL)
		return 0;
	return memhandle(rel, b, len, 1, 1, changed);
}

/* Read is files.c's Read: a length of -1 reads to the end, as 16 Mb did. */
uint32_t
Read(int32_t h, void *buf, uint32_t n)
{
	File *f;
	uint32_t k;

	f = file(h);
	if (f == NULL)
		return 0;
	if (n == 0xFFFFFFFF)
		n = 16000000;
	if (f->kind == Hdisk)
		return fread(buf, 1, n, f->f);
	if (!f->rd || f->pos >= f->len)
		return 0;
	k = f->len - f->pos;
	if (n < k)
		k = n;
	memcpy(buf, f->b + f->pos, k);
	f->pos += k;
	return k;
}

/* Room in f's bytes for end of them: 0, or -1 if memory is short. */
static int
grow(File *f, uint32_t end)
{
	uint8_t *b;
	uint32_t room;

	if (end <= f->room && f->b != NULL)
		return 0;
	room = f->room > 0 ? f->room : 4096;
	while (room < end)
		room = room > Maxfile/2 ? Maxfile : 2*room;
	b = realloc(f->b, room);
	if (b == NULL)
		return -1;
	f->b = b;
	f->room = room;
	return 0;
}

uint32_t
Write(int32_t h, void *buf, uint32_t n)
{
	File *f;
	uint64_t end;

	f = file(h);
	if (f == NULL)
		return 0;
	if (f->kind == Hdisk)
		return fwrite(buf, 1, n, f->f);
	if (!f->wr || n == 0)
		return 0;
	end = (uint64_t)f->pos + n;
	if (end > Maxfile || grow(f, end) < 0)
		return 0;
	if (f->pos > f->len)
		memset(f->b + f->len, 0, f->pos - f->len);
	memcpy(f->b + f->pos, buf, n);
	f->pos = end;
	if (f->len < f->pos)
		f->len = f->pos;
	f->changed = 1;
	return n;
}

void
Close(int32_t h)
{
	File *f;

	f = file(h);
	if (f == NULL)
		return;
	if (f->kind == Hdisk)
		fclose(f->f);
	else if (f->wr && f->changed)
		commit(f->name, f->b, f->len);
	else
		free(f->b);
	f->kind = Hfree;
	f->f = NULL;
	f->b = NULL;
}

/* Seek is lseek: the new position, or -1. */
int32_t
Seek(int32_t h, int32_t pos, int32_t mode)
{
	File *f;
	int64_t p;

	f = file(h);
	if (f == NULL)
		return -1;
	if (f->kind == Hdisk) {
		if (fseek(f->f, pos, mode) != 0)
			return -1;
		return ftell(f->f);
	}
	switch (mode) {
	case SEEK_SET:
		p = pos;
		break;
	case SEEK_CUR:
		p = (int64_t)f->pos + pos;
		break;
	case SEEK_END:
		p = (int64_t)f->len + pos;
		break;
	default:
		return -1;
	}
	if (p < 0 || p > INT32_MAX)
		return -1;
	f->pos = p;
	return p;
}

/*
 * renamefile: a file of the preferences renamed over another, 1 if it
 * was, 0 where either name lies outside them. A file's bytes move in
 * the view at once, the writer getting them whole as the target, and
 * the source's removal.
 */
int32_t
renamefile(char *from, char *to)
{
	char f[Npath], t[Npath], rf[Npath], rt[Npath];
	uint8_t *b;
	uint32_t len;
	Entry *sd, *s, *dd, *d;

	if (!ispref(from, rf, sizeof rf) || !ispref(to, rt, sizeof rt))
		return 0;
	if (keeping()) {
		reap();
		walk(rf, f, sizeof f, 0, &sd, &s);
		walk(rt, t, sizeof t, 0, &dd, &d);
		if (s == NULL || s->state == Edir || dd == NULL || (d != NULL && d->state == Edir))
			return 0;
		if (s == d)
			return 1;
		len = 0;
		if (s->state == Ebytes) {
			b = s->b;
			len = s->len;
			s->b = NULL;
		} else {
			b = slurp(f, &len);
			if (b == NULL)
				return 0;
		}
		d = spell(dd, lastpart(rt));
		free(d->b);
		d->state = Ebytes;
		d->b = b;
		d->len = len;
		d->mtime = s->mtime;
		d->job = queue(Jwrite, rt, t, b, len, 0);
		s->state = Egone;
		s->job = queue(Jremove, rf, f, NULL, 0, d->job);
		return 1;
	}
	if (under(dirs.pref, rf, f, sizeof f, 0) < 0 || isdir(f))
		return 0;
	under(dirs.pref, rt, t, sizeof t, 1);
	if (t[0] == '\0')
		return 0;
	return moveover(f, t) == 0;
}

/* Delete removes the preferences' file only: 1 if it did, 0 for a name outside them. */
int32_t
Delete(char *name)
{
	char path[Npath], rel[Npath];
	Entry *d, *e;

	if (!ispref(name, rel, sizeof rel))
		return 0;
	if (keeping()) {
		reap();
		walk(rel, path, sizeof path, 0, &d, &e);
		if (e == NULL || e->state == Edir)
			return 0;
		free(e->b);
		e->b = NULL;
		e->state = Egone;
		e->job = queue(Jremove, rel, path, NULL, 0, 0);
		return 1;
	}
	if (under(dirs.pref, rel, path, sizeof path, 0) < 0)
		return 0;
	return !isdir(path) && remove(path) == 0;
}

uint32_t
FileSize(char *name)
{
	char path[Npath], rel[Npath];
	const char *t;
	struct stat st;
	Entry *d, *e;
	int cd;

	if (ispref(name, rel, sizeof rel) && keeping()) {
		walk(rel, path, sizeof path, 0, &d, &e);
		if (e != NULL && (e->state == Edisk || e->state == Ebytes))
			return e->len;
	}
	cd = splitgame(name, rel, sizeof rel);
	if (cd < 0)
		return 0;
	if (readpath(name, path, sizeof path) == 0)
		return stat(path, &st) == 0 && !S_ISDIR(st.st_mode) ? st.st_size : 0;
	if (!cd && (t = textat(rel)) != NULL)
		return strlen(t);
	return 0;
}

/* Touch is files_a.asm's Touch: the preferences' file dated now, in the view and on the disk. */
void
Touch(char *name)
{
	char path[Npath], rel[Npath];
	Entry *d, *e;

	if (!ispref(name, rel, sizeof rel))
		return;
	if (keeping()) {
		reap();
		walk(rel, path, sizeof path, 0, &d, &e);
		if (e != NULL && (e->state == Edisk || e->state == Ebytes))
			e->mtime = time(NULL);
		if (e != NULL && e->state == Edisk)
			utime(path, NULL);
		return;
	}
	if (under(dirs.pref, rel, path, sizeof path, 0) == 0 && exists(path))
		utime(path, NULL);
}

/*
 * setwriter: the writer's lock (Wlock and its kin), whose thread runs
 * writer, or NULL, every job then done at once. It is set before the
 * game's first file, and taken off after endwrites. The view is read
 * again at its next use.
 */
void
setwriter(void (*fn)(int))
{
	reap();
	hook = fn;
	ending = 0;
	freeview(&top);
}

/* writer: the writer's thread, its jobs done in their order until endwrites ends it. */
void
writer(void)
{
	Job *j;
	int e;

	hook(Wlock);
	for (;;) {
		for (j = jobs; j != NULL && j->state != Jqueued; j = j->next)
			;
		if (j == NULL) {
			if (ending)
				break;
			hook(Wwait);
			continue;
		}
		j->state = Jbusy;
		hook(Wunlock);
		e = dojob(j);
		hook(Wlock);
		j->state = e == 0 ? Jdone : Jfailed;
		j->err = e;
		hook(Wwake);
	}
	hook(Wunlock);
}

/* stepwrite: the first job waiting done at once, for a test standing in for the writer's thread: 1 if one ran, else 0. */
int
stepwrite(void)
{
	Job *j;
	int e;

	for (j = jobs; j != NULL && j->state != Jqueued; j = j->next)
		;
	if (j == NULL)
		return 0;
	e = dojob(j);
	j->state = e == 0 ? Jdone : Jfailed;
	j->err = e;
	return 1;
}

/* endwrites: every job done, the writer's loop ended, the view settled. */
void
endwrites(void)
{
	Job *j;

	if (hook == NULL)
		return;
	hook(Wlock);
	for (;;) {
		for (j = jobs; j != NULL && (j->state == Jdone || j->state == Jfailed); j = j->next)
			;
		if (j == NULL)
			break;
		hook(Wwait);
	}
	ending = 1;
	hook(Wwake);
	hook(Wunlock);
	reap();
}

/*
 * GetHDFreeSize is func.asm's, DOS's free space on the current drive:
 * as much as the voices the game copies from the CD could want, the
 * same for every run, so that a replay takes the same path, and not so
 * much that the game's sums overflow.
 */
int32_t
GetHDFreeSize(void)
{
	return 512 << 20;
}

/*
 * AddExt is files.c's AddExt: the path's extension replaced, as
 * _splitpath and _makepath made it, the dot added if ext has none.
 */
void
AddExt(char *path, char *ext)
{
	char drive[3], dir[256], name[256], old[256];

	watcomsplitpath(path, drive, dir, name, old);
	strcpy(path, drive);
	strcat(path, dir);
	strcat(path, name);
	if (*ext != 0 && *ext != '.')
		strcat(path, ".");
	strcat(path, ext);
}

/*
 * DOS's search for files (_dos_findfirst, _dos_findnext): the entries of
 * the preferences' directory, then of each bind at the pattern's
 * directory or above it in turn, whose names match the pattern as DOS
 * matched one, its name and its extension each (dosmatch), the first
 * found of a name whatever its case, as a path finds it, the
 * directories too with _A_SUBDIR. On the CD and below the root a name
 * is 8.3, as the voice cache's names are sized for, and comes in upper
 * case, as DOS gave it, so that the cache, which compares its names
 * byte for byte, holds each file once. In the root, where the saves
 * lie, a name runs to Nroot characters in its own case, as LbaWin's and
 * the Classic's searches give them: DotEmu names a save by its date,
 * YYYYMMDDhhmmss.LBA. A name holding a \, a / or a :, which the game's
 * paths read as a directory or a drive, . and .., and the writer's
 * private files are left out. DOS gave them in the directory's order.
 * Here they come sorted by name whatever its case, every one of them,
 * the same on every system, so that a replay sees the same saves list.
 * A pattern too long for a path is DOS's path not found.
 */

enum
{
	Nfound = 512,	/* the found list's first entries, doubled as it grows */
	N83 = 12,	/* an 8.3 name's longest */
	Nroot = Nname-1 - (int)(sizeof privstart - 1) - (int)(sizeof privend - 1),	/* the root's longest, whose private name a host's 255 bytes hold */
	Nomore = 18,	/* DOS's error: no more files */
	Nodir = 3,	/* DOS's error: path not found */
};

typedef struct Found Found;

/* an entry _dos_findfirst found */
struct Found
{
	char		name[Nname];
	uint8_t		attrib;
	uint32_t	size;
	uint16_t	date;
	uint16_t	time;
};

static Found	*found;	/* the search's entries, maxfound of them allocated */
static size_t	nfound, maxfound, ifound;

/*
 * Whether the np bytes of a pattern's part match the ns of a name's, as
 * DOS matches them whatever the case: a ? any character, the blanks DOS
 * fills a name with past its end among them, a * the rest of the part
 * whatever follows it in the pattern.
 */
static int
match(const char *pat, size_t np, const char *s, size_t ns)
{
	size_t i;

	for (i = 0; i < np; i++) {
		if (pat[i] == '*')
			return 1;
		if (i >= ns) {
			if (pat[i] != '?')
				return 0;
		} else if (pat[i] != '?' && toupper((unsigned char)pat[i]) != toupper((unsigned char)s[i]))
			return 0;
	}
	return np >= ns;
}

/* Whether a file's name matches a DOS pattern: its name and its extension each. */
static int
dosmatch(const char *pat, const char *name)
{
	const char *pd, *nd;
	size_t pn, nn;

	pd = strrchr(pat, '.');
	nd = strrchr(name, '.');
	pn = pd != NULL ? (size_t)(pd - pat) : strlen(pat);
	nn = nd != NULL ? (size_t)(nd - name) : strlen(name);
	if (!match(pat, pn, name, nn))
		return 0;
	if (pd == NULL)
		return nd == NULL;
	return match(pd + 1, strlen(pd + 1), nd != NULL ? nd + 1 : "", nd != NULL ? strlen(nd + 1) : 0);
}

/*
 * Whether a search gives a host's name: of max characters at most, one
 * a DOS path names (without a \, / or :, which the game's paths read as
 * a directory or a drive, nor . or ..), no private file's, and where
 * max is N83 one DOS could hold, 8.3 without a space.
 */
static int
findable(const char *name, size_t max)
{
	const char *d;
	size_t n;

	n = strlen(name);
	if (n > max || strpbrk(name, "\\/:") != NULL || strcmp(name, ".") == 0 || strcmp(name, "..") == 0
	|| isprivate(name))
		return 0;
	if (max != N83)
		return 1;
	if (strchr(name, ' ') != NULL || name[0] == '.')
		return 0;
	d = strchr(name, '.');
	if (d == NULL)
		return n <= 8;
	return d - name <= 8 && strchr(d + 1, '.') == NULL && strlen(d + 1) <= 3;
}

/*
 * An entry found, its date and time DOS's words, kept where it is the
 * first of its name whatever its case, in upper case as DOS gave it
 * where the search's names are 8.3 (max N83).
 */
static void
keepfound(const char *name, size_t max, uint8_t attrib, uint32_t size, uint16_t date, uint16_t time)
{
	Found *f;
	size_t i, n;

	for (i = 0; i < nfound; i++)
		if (watcomstricmp(found[i].name, name) == 0)
			return;
	if (nfound == maxfound) {
		n = maxfound > 0 ? 2 * maxfound : Nfound;
		if (n > SIZE_MAX / sizeof found[0])
			return;
		f = realloc(found, n * sizeof found[0]);
		if (f == NULL)
			return;
		found = f;
		maxfound = n;
	}
	f = &found[nfound++];
	snprintf(f->name, sizeof f->name, "%s", name);
	if (max == N83)
		watcomstrupr(f->name);
	f->attrib = attrib;
	f->size = size;
	f->date = date;
	f->time = time;
}

/*
 * An entry of the host's found, kept as keepfound keeps one, its year
 * within DOS's 1980 to 2107, and a time localtime cannot give, as
 * mingw's before 1970, DOS's first day.
 */
static void
addfound(const char *name, size_t max, uint8_t attrib, uint32_t size, time_t mtime)
{
	struct tm *tm;
	int y;

	tm = localtime(&mtime);
	if (tm == NULL) {
		keepfound(name, max, attrib, size, 1<<5 | 1, 0);
		return;
	}
	y = tm->tm_year < 80 ? 0 : tm->tm_year - 80;
	if (y > 127)
		y = 127;
	keepfound(name, max, attrib, size, (uint16_t)(y<<9 | (tm->tm_mon + 1)<<5 | tm->tm_mday),
		(uint16_t)(tm->tm_hour<<11 | tm->tm_min<<5 | tm->tm_sec/2));
}

/*
 * The entries of the host's directory dir whose names match pat, of max
 * characters at most (findable), the directories too where subdirs is
 * set, but for a name whose path the port's paths cannot hold, which no
 * lookup reaches.
 */
static void
scan(const char *dir, const char *pat, int subdirs, size_t max)
{
	char file[Npath];
	struct stat st;
	struct dirent *e;
	DIR *d;

	d = opendir(dir);
	if (d == NULL)
		return;
	while ((e = readdir(d)) != NULL) {
		if (!findable(e->d_name, max) || !dosmatch(pat, e->d_name))
			continue;
		if (snprintf(file, sizeof file, "%s/%s", dir, e->d_name) >= (int)sizeof file)
			continue;
		if (stat(file, &st) < 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (subdirs)
				addfound(e->d_name, max, _A_SUBDIR, 0, st.st_mtime);
		} else
			addfound(e->d_name, max, _A_ARCH, (uint32_t)st.st_size, st.st_mtime);
	}
	closedir(d);
}

/* The entries of the view's directory d whose names match pat, as scan finds them on the disk. */
static void
scanview(Entry *d, const char *pat, int subdirs, size_t max)
{
	Entry *e;

	for (e = d->sub; e != NULL; e = e->next) {
		if (!findable(e->name, max) || !dosmatch(pat, e->name))
			continue;
		if (e->state == Edisk || e->state == Ebytes)
			addfound(e->name, max, _A_ARCH, e->len, e->mtime);
		else if (e->state == Edir && subdirs)
			addfound(e->name, max, _A_SUBDIR, 0, e->mtime);
	}
}

/* The part of path, a bind's, that names an entry of the directory rel, else NULL. */
static const char*
entryof(const char *path, const char *rel)
{
	size_t n;

	n = strlen(rel);
	if (n > 0 && (watcomstrnicmp(path, rel, n) != 0 || path[n] != '/'))
		return NULL;
	if (n > 0)
		path += n + 1;
	if (*path == '\0' || strchr(path, '/') != NULL)
		return NULL;
	return path;
}

static int
foundcmp(const void *a, const void *b)
{
	return watcomstricmp(((const Found*)a)->name, ((const Found*)b)->name);
}

static unsigned
nextfound(struct find_t *ft)
{
	Found *f;

	if (ifound >= nfound)
		return Nomore;
	f = &found[ifound++];
	memset(ft, 0, sizeof *ft);
	ft->attrib = (char)f->attrib;
	ft->wr_time = f->time;
	ft->wr_date = f->date;
	ft->size = f->size;
	snprintf(ft->name, sizeof ft->name, "%s", f->name);
	return 0;
}

unsigned
_dos_findfirst(const char *pattern, unsigned attr, struct find_t *ft)
{
	char rel[Npath], host[Npath], *dir, *pat;
	const char *e, *r;
	struct stat st;
	Entry *vd, *ve;
	size_t max;
	Bind *b;
	int cd, subdirs;

	nfound = 0;
	ifound = 0;
	cd = splitgame((char*)pattern, rel, sizeof rel);
	if (cd < 0)
		return Nodir;
	subdirs = (attr & _A_SUBDIR) != 0;
	pat = strrchr(rel, '/');
	if (pat != NULL) {
		*pat++ = 0;
		dir = rel;
	} else {
		pat = rel;
		dir = "";
	}
	max = N83;
	if (!cd) {
		if (dir[0] == 0)
			max = Nroot;
		if (keeping()) {
			walk(dir, host, sizeof host, 0, &vd, &ve);
			if (ve != NULL && ve->state == Edir)
				scanview(ve, pat, subdirs, max);
		} else if (under(dirs.pref, dir, host, sizeof host, 0) == 0)
			scan(host, pat, subdirs, max);
	}
	for (b = dirs.bind; b < dirs.bind + dirs.nbind; b++) {
		if (b->cd != cd)
			continue;
		if ((e = entryof(b->path, dir)) != NULL && findable(e, max) && dosmatch(pat, e)) {
			if (b->text != NULL)
				keepfound(e, max, _A_ARCH, strlen(b->text), 0, 0);
			else if (b->file && stat(b->dir, &st) == 0)
				addfound(e, max, _A_ARCH, st.st_size, st.st_mtime);
			else if (!b->file && subdirs)
				keepfound(e, max, _A_SUBDIR, 0, 0, 0);
		}
		if (b->file || (r = below(b->path, dir)) == NULL)
			continue;
		if (under(b->dir, r, host, sizeof host, 0) == 0)
			scan(host, pat, subdirs, max);
	}
	if (nfound > 1)
		qsort(found, nfound, sizeof found[0], foundcmp);
	return nextfound(ft);
}

unsigned
_dos_findnext(struct find_t *ft)
{
	return nextfound(ft);
}
