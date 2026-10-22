#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "dat.h"
#include "fns.h"
#include "dll.h"
#include "abi.h"
#include "pc.h"
#include "dosfs.h"

/*
 * DOS's file functions, int 21h's, over a host directory, the game's as
 * C:\LBA is, current, for the oracles: 3Ch, 3Dh, 3Eh, 3Fh, 40h (a count
 * of 0 cutting the file there), 41h, 42h, 4400h, 1Ah, 4Eh and 4Fh (the
 * search's place in the DTA's first 21 bytes), 2Ah and 2Ch (a clock of
 * the model's, two seconds on at each read), and 5700h and 5701h, a
 * name's parts, of 12 characters at most, cut to 8 and 3 characters in
 * upper case, as the host's directory holds DOS's names, 20 handles, 5
 * of them DOS's own, whose reads give 0 bytes and whose writes are
 * taken whole. dosfsinit gives the directory and closes what a run left
 * open, and dosfs is the handler pcint takes.
 */

enum
{
	Npath = 1024,
	Ndfile = 20,	/* DOS's handles */
	Nsearch = 64,
};

typedef struct Dfile Dfile;
typedef struct Search Search;

/* a file DOS holds open */
struct Dfile
{
	FILE	*f;
	int	mode;	/* 0 to read, 1 to write, 2 both */
	int64_t	pos;
	char	path[Npath];	/* the host's */
};

/* a search under way, its files' names */
struct Search
{
	char	dir[Npath];
	char	(*name)[13];
	int	n;
};

static Dfile dfile[Ndfile];
static Search search[Nsearch];	/* the last searches, a ring */
static uint32_t nsearch;
static uint8_t *dta;
static uint32_t dclock;	/* the model's DOS clock, seconds since 1994-10-14 */
static char dosdir[Npath/2];

/* The directory as C:\LBA, the handles a run left open closed, the searches forgotten. */
void
dosfsinit(char *dir)
{
	int i;

	snprintf(dosdir, sizeof dosdir, "%s", dir);
	for (i = 5; i < Ndfile; i++) {
		if (dfile[i].f != NULL)
			fclose(dfile[i].f);
		dfile[i].f = NULL;
	}
	nsearch = 0;
	dclock = 0;
}

/* The model's error: CF and AX's code. */
static void
doserr(Cpu *c, int code)
{
	c->eax = code;
	c->flags |= 1;
}

static char*
flat(Cpu *c, uint32_t off)
{
	return (char*)cpumem(c, c->ds, off, 1);
}

/* A name's part as DOS keeps it: upper case, 8 and 3 characters, in out. */
static void
dospart(char *p, int n, char *out)
{
	char base[9], ext[4];
	int i, k, dot;

	dot = -1;
	for (i = 0; i < n; i++)
		if (p[i] == '.')
			dot = i;
	if (dot == 0 || (n <= 2 && p[0] == '.')) {
		memmove(out, p, n);
		out[n] = 0;
		return;
	}
	k = 0;
	for (i = 0; i < (dot >= 0 ? dot : n) && k < 8; i++)
		base[k++] = toupper((unsigned char)p[i]);
	base[k] = 0;
	k = 0;
	if (dot >= 0)
		for (i = dot+1; i < n && k < 3; i++)
			ext[k++] = toupper((unsigned char)p[i]);
	ext[k] = 0;
	if (ext[0] != 0)
		sprintf(out, "%s.%s", base, ext);
	else
		strcpy(out, base);
}

/* A DOS path, relative to C:\LBA, as the host has it under dosdir: 0, or -1 for one DOS refuses. */
static int
dospath(char *p, char *out, int nout)
{
	char part[16];
	char *s, *e;
	int n;

	if (p[0] != 0 && p[1] == ':') {
		if (toupper((unsigned char)p[0]) != 'C')
			return -1;
		p += 2;
	}
	if (p[0] == '\\' || p[0] == '/')
		return -1;
	n = snprintf(out, nout, "%s", dosdir);
	for (s = p; *s != 0 && n < nout; s = e) {
		for (e = s; *e != 0 && *e != '\\' && *e != '/'; e++)
			;
		if (e == s || e - s > 12)
			return -1;
		dospart(s, e - s, part);
		n += snprintf(out + n, nout - n, "/%s", part);
		if (*e != 0)
			e++;
	}
	return n < nout ? 0 : -1;
}

static int
isdir(char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
isfile(char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int
freehandle(void)
{
	int i;

	for (i = 5; i < Ndfile; i++)
		if (dfile[i].f == NULL)
			return i;
	return -1;
}

static Dfile*
handle(Cpu *c)
{
	uint32_t h;

	h = c->ebx & 0xffff;
	if (h >= Ndfile || dfile[h].f == NULL) {
		doserr(c, 6);
		return NULL;
	}
	return &dfile[h];
}

/* An FCB's 11 characters of a part, a pattern's * filling with ?. */
static void
fcb(char *p, char *out)
{
	char *dot;
	int i, k;

	memset(out, ' ', 11);
	dot = strrchr(p, '.');
	for (i = 0, k = 0; p + i != dot && p[i] != 0 && k < 8; i++) {
		if (p[i] == '*') {
			while (k < 8)
				out[k++] = '?';
			break;
		}
		out[k++] = toupper((unsigned char)p[i]);
	}
	if (dot == NULL)
		return;
	for (i = 1, k = 8; dot[i] != 0 && k < 11; i++) {
		if (dot[i] == '*') {
			while (k < 11)
				out[k++] = '?';
			break;
		}
		out[k++] = toupper((unsigned char)dot[i]);
	}
}

static int
fcbmatch(char *pat, char *name)
{
	char a[11], b[11];
	int i;

	fcb(pat, a);
	fcb(name, b);
	for (i = 0; i < 11; i++)
		if (a[i] != '?' && a[i] != b[i])
			return 0;
	return 1;
}

static int
namecmp(const void *a, const void *b)
{
	return strcasecmp(a, b);
}

/* The DTA filled from the search's next file: 0, or DOS's 18 past the last. */
static int
nextfile(void)
{
	char path[Npath];
	struct stat st;
	Search *s;
	uint32_t id, i, t;

	id = get32(dta);
	i = get32(dta + 4);
	if (id >= nsearch || nsearch - id > Nsearch)
		return 18;
	s = &search[id % Nsearch];
	if (i >= (uint32_t)s->n)
		return 18;
	snprintf(path, sizeof path, "%s/%s", s->dir, s->name[i]);
	if (stat(path, &st) < 0)
		return 18;
	put32(dta + 4, i + 1);
	dta[21] = S_ISDIR(st.st_mode) ? 0x10 : 0x20;
	t = dclock;
	put16(dta + 22, (t / 3600 % 24) << 11 | (t / 60 % 60) << 5 | (t % 60) / 2);
	put16(dta + 24, 14 << 9 | 10 << 5 | 14);
	put32(dta + 26, st.st_size);
	memset(dta + 30, 0, 13);
	strcpy((char*)dta + 30, s->name[i]);
	return 0;
}

/* 4Eh: the files of the pattern's directory that match it, sorted, the first in the DTA. */
static void
findfirst(Cpu *c)
{
	char path[Npath], entry[2*Npath], *slash;
	struct dirent *d;
	Search *s;
	DIR *dp;
	char *p;

	p = flat(c, c->edx);
	if (dospath(p, path, sizeof path) < 0) {
		doserr(c, 3);
		return;
	}
	slash = strrchr(path, '/');
	*slash = 0;
	p = strrchr(p, '\\') != NULL ? strrchr(p, '\\') + 1 : p;
	s = &search[nsearch % Nsearch];
	snprintf(s->dir, sizeof s->dir, "%s", path);
	s->n = 0;
	free(s->name);
	s->name = NULL;
	dp = opendir(path);
	if (dp == NULL) {
		doserr(c, 3);
		return;
	}
	while ((d = readdir(dp)) != NULL) {
		if (d->d_name[0] == '.' || strlen(d->d_name) > 12 || !fcbmatch(p, d->d_name))
			continue;
		snprintf(entry, sizeof entry, "%s/%s", s->dir, d->d_name);
		if (isdir(entry) && !(c->ecx & 0x10))
			continue;
		s->name = realloc(s->name, (s->n + 1) * sizeof s->name[0]);
		if (s->name == NULL)
			fatal("out of memory");
		snprintf(s->name[s->n++], 13, "%s", d->d_name);
	}
	closedir(dp);
	qsort(s->name, s->n, sizeof s->name[0], namecmp);
	put32(dta, nsearch++);
	put32(dta + 4, 0);
	if (nextfile() != 0)
		doserr(c, 18);
}

/* DOS's int 21h, its file functions. */
void
dosfs(Cpu *c)
{
	char path[Npath];
	Dfile *f;
	uint32_t n, v;
	int64_t p;
	int h;

	c->flags &= ~1u;
	switch (c->eax >> 8 & 0xff) {
	case 0x1a:
		dta = (uint8_t*)flat(c, c->edx);
		return;
	case 0x2a:
		setlo(&c->ecx, 1994);
		c->edx = (c->edx & ~0xffffu) | 10 << 8 | 14;
		c->eax = (c->eax & ~0xffu) | 5;
		return;
	case 0x2c:
		dclock += 2;
		c->ecx = (c->ecx & ~0xffffu) | (12 + dclock / 3600 % 12) << 8 | (dclock / 60 % 60);
		c->edx = (c->edx & ~0xffffu) | (dclock % 60) << 8;
		return;
	case 0x3c:
	case 0x3d:
		if (dospath(flat(c, c->edx), path, sizeof path) < 0) {
			doserr(c, 3);
			return;
		}
		if (isdir(path)) {
			doserr(c, 5);
			return;
		}
		h = freehandle();
		if (h < 0) {
			doserr(c, 4);
			return;
		}
		f = &dfile[h];
		if ((c->eax >> 8 & 0xff) == 0x3c) {
			f->f = fopen(path, "w+b");
			f->mode = 2;
		} else {
			if (!isfile(path)) {
				doserr(c, 2);
				return;
			}
			f->mode = c->eax & 3;
			f->f = fopen(path, f->mode == 0 ? "rb" : "r+b");
		}
		if (f->f == NULL) {
			doserr(c, 3);
			return;
		}
		f->pos = 0;
		snprintf(f->path, sizeof f->path, "%s", path);
		c->eax = h;
		return;
	case 0x3e:
		if ((c->ebx & 0xffff) < 5)
			return;
		if ((f = handle(c)) == NULL)
			return;
		fclose(f->f);
		f->f = NULL;
		return;
	case 0x3f:
		if ((c->ebx & 0xffff) < 5) {
			c->eax = 0;
			return;
		}
		if ((f = handle(c)) == NULL)
			return;
		if (f->mode == 1 || f->pos < 0) {
			doserr(c, 5);
			return;
		}
		fseek(f->f, f->pos, SEEK_SET);
		n = fread(flat(c, c->edx), 1, c->ecx, f->f);
		f->pos += n;
		c->eax = n;
		return;
	case 0x40:
		if ((c->ebx & 0xffff) < 5) {
			c->eax = c->ecx;
			return;
		}
		if ((f = handle(c)) == NULL)
			return;
		if (f->mode == 0 || f->pos < 0) {
			doserr(c, 5);
			return;
		}
		fflush(f->f);
		if (c->ecx == 0) {
			if (ftruncate(fileno(f->f), f->pos) < 0)
				fatal("%s: cannot cut it", f->path);
			c->eax = 0;
			return;
		}
		fseek(f->f, f->pos, SEEK_SET);
		n = fwrite(flat(c, c->edx), 1, c->ecx, f->f);
		fflush(f->f);
		f->pos += n;
		c->eax = n;
		return;
	case 0x41:
		if (dospath(flat(c, c->edx), path, sizeof path) < 0) {
			doserr(c, 3);
			return;
		}
		if (!isfile(path)) {
			doserr(c, isdir(path) ? 5 : 2);
			return;
		}
		if (remove(path) < 0)
			doserr(c, 5);
		return;
	case 0x42:
		if ((f = handle(c)) == NULL)
			return;
		v = (c->ecx & 0xffff) << 16 | (c->edx & 0xffff);
		switch (c->eax & 0xff) {
		case 0:
			p = (int32_t)v;
			break;
		case 1:
			p = f->pos + (int32_t)v;
			break;
		case 2:
			fseek(f->f, 0, SEEK_END);
			p = ftell(f->f) + (int32_t)v;
			break;
		default:
			doserr(c, 1);
			return;
		}
		f->pos = p;
		c->eax = (c->eax & ~0xffffu) | (p & 0xffff);
		c->edx = (c->edx & ~0xffffu) | (p >> 16 & 0xffff);
		return;
	case 0x4e:
		findfirst(c);
		return;
	case 0x4f:
		if (nextfile() != 0)
			doserr(c, 18);
		return;
	case 0x44:
		if ((c->eax & 0xff) != 0)
			break;
		if ((c->ebx & 0xffff) < 5) {
			setlo(&c->edx, 0x80d3);
			return;
		}
		if ((f = handle(c)) == NULL)
			return;
		setlo(&c->edx, 2);
		return;
	case 0x57:
		if ((f = handle(c)) == NULL)
			return;
		if ((c->eax & 0xff) == 0) {
			setlo(&c->ecx, 0);
			setlo(&c->edx, 0);
		}
		return;
	}
	fail("int 21h, function %04x, which the model lacks", c->eax & 0xffff);
	runend();
}

