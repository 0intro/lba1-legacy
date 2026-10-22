#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * A CD's image read as the CD: a cue sheet and the files it names, or an
 * ISO, the data track alone in sectors of 2048 bytes. The cue sheet's
 * FILE, TRACK and INDEX lines place each track in the file of its INDEX
 * 01, one file for the disc or one for each track: the data track
 * MODE1/2352, MODE1/2048 or MODE2/2352 (the PlayStation's, its sectors of
 * Form 1), the audio tracks 2352 bytes a frame. A track's INDEX 00 lies in
 * that file, or at the end of the file of the track before, where Exact
 * Audio Copy appends the gap. A BINARY file holds its sectors raw, and
 * a WAVE file the audio tracks' samples, 16-bit stereo at 44100 Hz. A
 * BINARY file's samples are little-endian, as BINARY says, unless the
 * image's step less than a quarter as much read big-endian, from each
 * sample to the next of its channel, at Nprobe places of each audio
 * track of a BINARY file (bigendian), as the Internet Archive's rip of
 * LBA2's EU CD holds its track 2. A sound steps little, and a sample whose
 * bytes are read in the wrong order jumps by its low byte, as noise. The
 * data track's ISO 9660 directories give its files, a name compared
 * whatever its case once its version (a semicolon and a number) and a
 * last dot are taken off, and a MODE1/2352 sector holds its 2048 bytes
 * after 16 of sync and header, a MODE2/2352 sector after 24, its
 * subheader's 8 among them. An audio track runs from its INDEX 01 to the
 * next track's, the extent a CD's table of contents gives it, as
 * tools/cdtracks.py cuts it: the next track's PREGAP among it as
 * silence, before that track's INDEX 00, else its INDEX 01. A track that
 * ends its file runs on through the next file's frames before the next
 * track's INDEX 01, as the disc plays them (imagetrackread). A file of
 * the directory whose extent lies in an audio track, as the
 * PlayStation's disc places its jingles, is read as that track's
 * frames, a frame for each 2048 bytes of its size (imagesound), as
 * tools/isocat.py copies it. A disc whose root names its tunes,
 * TRACK1.WAV at an audio track's INDEX 01 as on the PlayStation's,
 * gives TRACKnn.WAV as the track its TRACKn.WAV for tune nn-1 starts,
 * the PC CD's track for that tune. Every
 * number read is checked against the bytes that hold it: a malformed cue
 * sheet or volume is refused, and a malformed directory record fails the
 * lookup that reads it.
 *
 * A file of raw sectors given in its sheet's place, known by the sync
 * that starts a data sector, is refused, and its sheet asked for.
 *
 * A sheet's files are named from its directory or from the root, and
 * its lines 1022 bytes at most before their line feed. Its other
 * commands, POSTGAP among them, and its INDEX lines past 01 are passed
 * over. The volume's sectors count from the data track's INDEX 01, as on
 * a disc whose first track it is, and the volume is read through its
 * primary descriptor alone, without Joliet's names, a directory 64
 * sectors at most. A file is read at offsets held in a long, so that a
 * file past 2 GB, more than a CD holds, is refused on Windows and the
 * 32-bit builds.
 */

enum
{
	Sector = 2048,	/* a data sector's bytes */
	Raw = 2352,	/* a raw sector's, an audio frame's */
	Chunk = Raw/4,	/* an audio frame's samples, 588 */
	Rate = 44100,
	Ntrack = 99,
	Pvd = 16,	/* the primary volume descriptor's sector */
	Rootrec = 156,	/* the root's directory record, in the descriptor */
	Nrec = 33,	/* a directory record's bytes before its name */
	Maxdir = 64,	/* a directory's sectors at most */
	Nline = 1024,
	Nname = 256,
	Npath = 1024,
	Nprobe = 16,	/* the places of an audio track whose samples tell their order */
	Probe = 4,	/* the frames read at each */
};

/* A file's kind */
enum
{
	Fbinary,	/* raw sectors */
	Fwave,	/* a RIFF WAVE file's samples */
};

typedef struct Kind Kind;
typedef struct Itrack Itrack;

/* A kind of track a cue sheet names: its sectors' bytes, the offset of a data sector's 2048 bytes in one, whether it is audio. */
struct Kind
{
	char	*name;
	int	size;
	int	head;
	int	audio;
};

static Kind kinds[] = {
	{"AUDIO", Raw, 0, 1},
	{"MODE1/2352", Raw, 16, 0},	/* after sync and header */
	{"MODE1/2048", Sector, 0, 0},
	{"MODE2/2352", Raw, 24, 0},	/* after sync, header and Form 1's subheader */
};

/* A track of the cue sheet. */
struct Itrack
{
	int32_t	num;
	int	audio;
	int	size;	/* the bytes of its sectors, Raw or Sector */
	int	head;	/* a data sector's 2048 bytes' offset in one */
	int	file;	/* its INDEX 01's file's index */
	uint32_t	start;	/* its INDEX 01, in its file's sectors */
	int	idx0file;	/* its INDEX 00's file's index */
	uint32_t	idx0;	/* its INDEX 00, in that file's sectors, else UINT32_MAX */
	uint32_t	gap;	/* its PREGAP's sectors of silence */
	uint32_t	lba;	/* its INDEX 01 among the volume's sectors, UINT32_MAX before the data track */
};

struct Image
{
	char	*path[Ntrack];	/* the files, in the sheet's order */
	FILE	*f[Ntrack];	/* a BINARY or WAVE file's */
	int	kind[Ntrack];
	uint32_t	base[Ntrack];	/* a WAVE file's first byte of samples */
	uint32_t	len[Ntrack];	/* and their bytes */
	uint32_t	nsec[Ntrack];	/* each file's sectors */
	int	size[Ntrack];	/* the bytes of each file's sectors, Raw or Sector */
	int	nfile;
	Itrack	t[Ntrack];
	int	nt;
	int	data;	/* the data track's index */
	uint32_t	ndata;	/* its sectors */
	uint32_t	root;	/* the root directory's first sector */
	uint32_t	rootsize;	/* its bytes */
	char	label[33];	/* the volume's identifier */
	int	swap;	/* the BINARY files' samples big-endian */
	int	named;	/* its root names its tunes TRACKn.WAV, the PlayStation's */
};

/* the sync that starts each data sector of a raw image */
static const uint8_t sync[12] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};

static uint32_t
le32(uint8_t *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24;
}

static void
seterr(char *err, int n, char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	vsnprintf(err, n, fmt, arg);
	va_end(arg);
}

/* The sectors of f, of size bytes each, into *n: 0, or -1. */
static int
sectors(FILE *f, int size, uint32_t *n)
{
	long len;

	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0)
		return -1;
	if ((unsigned long)len / size > UINT32_MAX)
		return -1;
	*n = len / size;
	return 0;
}

/* mm:ss:ff's frames, or -1. */
static int32_t
msf(char *s)
{
	int m, sec, f, k;

	k = 0;
	if (sscanf(s, "%2d:%2d:%2d%n", &m, &sec, &f, &k) != 3 || s[k] != '\0')
		return -1;
	if (m < 0 || sec < 0 || sec > 59 || f < 0 || f > 74)
		return -1;
	return (m*60 + sec)*75 + f;
}

/* The sheet's FILE line's name into name and its kind into kind: 0, or -1. */
static int
fileline(char *s, char *name, char *kind)
{
	char *e;
	int n;

	while (*s == ' ' || *s == '\t')
		s++;
	if (*s == '"') {
		e = strchr(s+1, '"');
		if (e == NULL || e == s+1)
			return -1;
		n = e - (s+1);
		memcpy(name, s+1, n);
		name[n] = '\0';
		s = e + 1;
	} else if (sscanf(s, "%1023s%n", name, &n) == 1)
		s += n;
	else
		return -1;
	return sscanf(s, "%15s", kind) == 1 ? 0 : -1;
}

/*
 * A WAVE file's samples: their first byte into *base, their bytes into
 * *len, those of the file at most, and their frames of 2352 bytes into
 * *n, for samples of 16 bits, stereo, at 44100 Hz: 0, or -1, a chunk
 * running past the file's end refused.
 */
static int
wavedata(FILE *f, uint32_t *base, uint32_t *len, uint32_t *n)
{
	uint8_t h[12], c[8], fmt[16];
	uint64_t at, size;
	uint32_t k;
	long end;
	int havefmt;

	if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0)
		return -1;
	size = (unsigned long)end < UINT32_MAX ? (uint64_t)end : UINT32_MAX;
	if (fread(h, 1, sizeof h, f) != sizeof h || memcmp(h, "RIFF", 4) != 0 || memcmp(h+8, "WAVE", 4) != 0)
		return -1;
	at = sizeof h;
	havefmt = 0;
	while (at + sizeof c <= size && fread(c, 1, sizeof c, f) == sizeof c) {
		k = le32(c+4);
		at += sizeof c;
		if (memcmp(c, "data", 4) == 0) {
			if (!havefmt)
				return -1;
			if (k > size - at)
				k = size - at;
			*base = at;
			*len = k;
			*n = (k/4 + Chunk-1) / Chunk;
			return 0;
		}
		if (memcmp(c, "fmt ", 4) == 0 && k >= sizeof fmt) {
			if (fread(fmt, 1, sizeof fmt, f) != sizeof fmt)
				return -1;
			if (fmt[0] != 1 || fmt[1] != 0 || fmt[2] != 2 || fmt[3] != 0 || le32(fmt+4) != Rate || fmt[14] != 16)
				return -1;
			havefmt = 1;
		}
		at += (uint64_t)k + (k&1);
		if (at > size || fseek(f, (long)at, SEEK_SET) != 0)
			return -1;
	}
	return -1;
}

/*
 * The file name of the sheet at cue, of kind, found beside the sheet
 * whatever its case, or from the root for a name that starts there,
 * opened: 0, or -1.
 */
static int
addfile(Image *m, char *cue, char *name, char *kind, char *err, int nerr)
{
	char dir[Npath], path[Npath], *p, *q, *rel;
	int i;

	if (m->nfile == Ntrack) {
		seterr(err, nerr, "more than %d files", Ntrack);
		return -1;
	}
	for (p = name; *p != '\0'; p++)
		if (*p == '\\')
			*p = '/';
	rel = name;
	if (name[0] == '/')
		dir[0] = '\0';
#ifdef _WIN32
	else if (isalpha((unsigned char)name[0]) && name[1] == ':' && name[2] == '/') {
		snprintf(dir, sizeof dir, "%.2s", name);
		rel = name + 2;
	}
#endif
	else {
		p = strrchr(cue, '/');
		q = strrchr(cue, '\\');
		if (q != NULL && (p == NULL || q > p))
			p = q;
		if (p == NULL)
			snprintf(dir, sizeof dir, ".");
		else if (p == cue)
			snprintf(dir, sizeof dir, "/");
		else
			snprintf(dir, sizeof dir, "%.*s", (int)(p - cue), cue);
	}
	if (findhost(dir, rel, path, sizeof path) < 0) {
		seterr(err, nerr, "%s: no such file", name);
		return -1;
	}
	i = m->nfile;
	m->path[i] = malloc(strlen(path)+1);
	if (m->path[i] == NULL) {
		seterr(err, nerr, "no memory");
		return -1;
	}
	strcpy(m->path[i], path);
	m->nfile++;
	if (watcomstricmp(kind, "BINARY") == 0) {
		m->kind[i] = Fbinary;
		m->f[i] = fopen(path, "rb");
	} else if (watcomstricmp(kind, "WAVE") == 0) {
		m->kind[i] = Fwave;
		m->f[i] = fopen(path, "rb");
		if (m->f[i] != NULL && wavedata(m->f[i], &m->base[i], &m->len[i], &m->nsec[i]) < 0) {
			seterr(err, nerr, "%s: a WAVE file other than 16-bit stereo at 44100 Hz", name);
			return -1;
		}
		m->size[i] = Raw;
	} else {
		seterr(err, nerr, "%s: a file of a kind other than BINARY or WAVE", name);
		return -1;
	}
	if (m->f[i] == NULL) {
		seterr(err, nerr, "%s", strerror(errno));
		return -1;
	}
	return 0;
}

/* The cue sheet f, at path, read into m: 0, or -1 with err set. */
static int
cue(Image *m, FILE *f, char *path, char *err, int nerr)
{
	char line[Nline], word[16], name[Nline], kind[16], time[16];
	Itrack *t;
	Kind *k;
	int32_t start;
	char *s;
	int i, n, num, idx;

	while (fgets(line, sizeof line, f) != NULL) {
		n = strlen(line);
		if (n == sizeof line - 1 && line[n-1] != '\n') {
			seterr(err, nerr, "a line too long");
			return -1;
		}
		while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ' || line[n-1] == '\t'))
			line[--n] = '\0';
		s = line;
		if (strncmp(s, "\xEF\xBB\xBF", 3) == 0)
			s += 3;
		if (sscanf(s, "%15s%n", word, &n) != 1)
			continue;
		s += n;
		if (watcomstricmp(word, "FILE") == 0) {
			if (fileline(s, name, kind) < 0) {
				seterr(err, nerr, "a FILE line without its name and kind");
				return -1;
			}
			if (addfile(m, path, name, kind, err, nerr) < 0)
				return -1;
		} else if (watcomstricmp(word, "TRACK") == 0) {
			if (m->nfile == 0 || m->nt == Ntrack || sscanf(s, "%d %15s", &num, kind) != 2) {
				seterr(err, nerr, "a TRACK line out of place");
				return -1;
			}
			for (k = kinds; k < kinds + nelem(kinds) && watcomstricmp(kind, k->name) != 0; k++)
				;
			if (k == kinds + nelem(kinds)) {
				seterr(err, nerr, "%s: a track of a kind other than AUDIO, MODE1/2352, MODE1/2048 or MODE2/2352", kind);
				return -1;
			}
			if (num < 1 || num > Ntrack || (m->nt > 0 && num <= m->t[m->nt-1].num)) {
				seterr(err, nerr, "a track's number out of order");
				return -1;
			}
			t = &m->t[m->nt++];
			t->num = num;
			t->audio = k->audio;
			t->size = k->size;
			t->head = k->head;
			t->start = UINT32_MAX;
			t->idx0 = UINT32_MAX;
			t->gap = 0;
		} else if (watcomstricmp(word, "PREGAP") == 0) {
			/* between its track's TRACK line and INDEX lines */
			if (m->nt == 0 || m->t[m->nt-1].start != UINT32_MAX || m->t[m->nt-1].idx0 != UINT32_MAX
			|| sscanf(s, "%15s", time) != 1 || (start = msf(time)) < 0) {
				seterr(err, nerr, "a PREGAP line out of place");
				return -1;
			}
			m->t[m->nt-1].gap = start;
		} else if (watcomstricmp(word, "INDEX") == 0) {
			if (m->nt == 0 || sscanf(s, "%d %15s", &idx, time) != 2) {
				seterr(err, nerr, "an INDEX line out of place");
				return -1;
			}
			if (idx < 0) {
				seterr(err, nerr, "an INDEX's number out of range");
				return -1;
			}
			if (idx > 1)
				continue;
			start = msf(time);
			if (start < 0) {
				seterr(err, nerr, "%s: not a time of mm:ss:ff", time);
				return -1;
			}
			t = &m->t[m->nt-1];
			i = m->nfile - 1;
			if ((idx == 0 && t->idx0 != UINT32_MAX) || (idx == 1 && t->start != UINT32_MAX)) {
				seterr(err, nerr, "a track's INDEX %02d given twice", idx);
				return -1;
			}
			if (m->kind[i] != Fbinary && !t->audio) {
				seterr(err, nerr, "a data track in a file of sound");
				return -1;
			}
			if (m->size[i] != 0 && m->size[i] != t->size) {
				seterr(err, nerr, "tracks of two sector sizes in one file");
				return -1;
			}
			m->size[i] = t->size;
			if (idx == 0) {
				t->idx0file = i;
				t->idx0 = start;
			} else {
				t->file = i;
				t->start = start;
			}
		}
	}
	return 0;
}

/* The data track's sector s, its 2048 bytes into b: 0, or -1. */
static int
datasector(Image *m, uint32_t s, uint8_t *b)
{
	Itrack *t;
	uint64_t off;
	int i;

	if (s >= m->ndata)
		return -1;
	t = &m->t[m->data];
	i = t->file;
	off = ((uint64_t)t->start + s) * m->size[i] + t->head;
	if (off > LONG_MAX || fseek(m->f[i], (long)off, SEEK_SET) != 0)
		return -1;
	return fread(b, 1, Sector, m->f[i]) == Sector ? 0 : -1;
}

/* The sectors after track i's INDEX 01 up to the next track's in its file, or to its file's end. */
static uint32_t
trackspan(Image *m, int i)
{
	Itrack *t;

	t = &m->t[i];
	if (i+1 < m->nt && m->t[i+1].file == t->file)
		return m->t[i+1].start - t->start;
	return m->nsec[t->file] - t->start;
}

/* Whether the extent at s of size bytes lies within the data track. */
static int
inside(Image *m, uint32_t s, uint32_t size)
{
	return s < m->ndata && (size + (uint64_t)Sector-1)/Sector <= m->ndata - s;
}

/* The record's name into s, its version and a last dot taken off. */
static void
recname(uint8_t *rec, char *s)
{
	char *p;
	int n;

	n = rec[32];
	memcpy(s, rec+Nrec, n);
	s[n] = '\0';
	p = strchr(s, ';');
	if (p != NULL)
		*p = '\0';
	n = strlen(s);
	if (n > 0 && s[n-1] == '.')
		s[n-1] = '\0';
}

/*
 * Each record of the directory at s, of size bytes, given to fn with
 * arg until fn returns 1: 1 then, 0 at the directory's end, -1 if a
 * record is malformed.
 */
static int
eachrec(Image *m, uint32_t s, uint32_t size, int (*fn)(void*, uint8_t*), void *arg)
{
	uint8_t b[Sector];
	uint32_t i, n, off, len;

	if (!inside(m, s, size))
		return -1;
	n = (size + Sector-1)/Sector;
	if (n > Maxdir)
		return -1;
	for (i = 0; i < n; i++) {
		if (datasector(m, s+i, b) < 0)
			return -1;
		for (off = 0; off < Sector && b[off] != 0; off += len) {
			len = b[off];
			if (len < Nrec+1 || off+len > Sector || (uint32_t)Nrec + b[off+32] > len)
				return -1;
			if (fn(arg, b+off) == 1)
				return 1;
		}
	}
	return 0;
}

typedef struct Want Want;

/* A name sought in a directory, and its record when found. */
struct Want
{
	char	*name;
	uint8_t	rec[Nrec];
};

static int
wanted(void *arg, uint8_t *rec)
{
	char s[Nname];
	Want *w;

	w = arg;
	recname(rec, s);
	if ((rec[25] & 4) || watcomstricmp(s, w->name) != 0)
		return 0;
	memcpy(w->rec, rec, Nrec);
	return 1;
}

/* The first sector, bytes and directory flag of rel, a path with slashes, "" for the root, a directory within the data track: 0, or -1. */
static int
walk(Image *m, char *rel, uint32_t *s, uint32_t *size, int *dir)
{
	char part[Nname], *p, *q;
	Want w;
	int n;

	*s = m->root;
	*size = m->rootsize;
	*dir = 1;
	for (p = rel; *p != '\0'; p = q) {
		q = strchr(p, '/');
		n = q != NULL ? q - p : (int)strlen(p);
		q = q != NULL ? q+1 : p+n;
		if (n == 0)
			continue;
		if (!*dir || n >= Nname)
			return -1;
		memcpy(part, p, n);
		part[n] = '\0';
		w.name = part;
		if (eachrec(m, *s, *size, wanted, &w) != 1)
			return -1;
		*s = le32(w.rec+2);
		*size = le32(w.rec+10);
		*dir = (w.rec[25] & 2) != 0;
		if (*dir && !inside(m, *s, *size))
			return -1;
	}
	return 0;
}

/* imagefile: rel's file in the data track, its first sector into *s and its bytes into *size: 0, or -1. */
int
imagefile(Image *m, char *rel, uint32_t *s, uint32_t *size)
{
	int dir;

	if (walk(m, rel, s, size, &dir) < 0 || dir || !inside(m, *s, *size))
		return -1;
	return 0;
}

/* imagedir: whether rel names a directory of the data track. */
int
imagedir(Image *m, char *rel)
{
	uint32_t s, size;
	int dir;

	return walk(m, rel, &s, &size, &dir) == 0 && dir;
}

/*
 * imageread: n bytes of the file at its first sector s, from its byte
 * pos, into buf: the count read, short at a sector the image fails to
 * give.
 */
uint32_t
imageread(Image *m, uint32_t s, uint32_t pos, uint8_t *buf, uint32_t n)
{
	uint8_t b[Sector];
	uint32_t done, o, k;

	for (done = 0; done < n; done += k) {
		o = (pos + done) % Sector;
		k = Sector - o;
		if (k > n - done)
			k = n - done;
		if (datasector(m, s + (pos + done)/Sector, b) < 0)
			break;
		memcpy(buf+done, b+o, k);
	}
	return done;
}

typedef struct Lister Lister;

/* imagelist's call of fn with arg, for the image m's entries. */
struct Lister
{
	Image	*m;
	void	(*fn)(void*, char*, int, uint32_t, uint16_t, uint16_t);
	void	*arg;
};

static int
listed(void *arg, uint8_t *rec)
{
	Lister *l;
	char s[Nname];
	uint16_t date, time;
	int y, dir;

	l = arg;
	dir = (rec[25] & 2) != 0;
	if ((rec[25] & 4) || (!dir && !inside(l->m, le32(rec+2), le32(rec+10))))
		return 0;
	recname(rec, s);
	/* the directory's . and .., names of a byte 0 and 1 */
	if (s[0] == '\0' || (rec[32] == 1 && rec[Nrec] == 1))
		return 0;
	/* the record's years since 1900, DOS's since 1980 */
	y = rec[18] < 80 ? 0 : rec[18] - 80;
	if (y > 127)
		y = 127;
	date = y<<9 | (rec[19] & 15)<<5 | (rec[20] & 31);
	time = (rec[21] & 31)<<11 | (rec[22] & 63)<<5 | (rec[23] & 63)/2;
	l->fn(l->arg, s, dir, dir ? 0 : le32(rec+10), date, time);
	return 0;
}

/*
 * imagelist: each entry of the directory rel within the data track given
 * to fn with arg: its name, whether it is a directory, its bytes and its
 * date as DOS's date and time words. 0, or -1 for no such directory.
 */
int
imagelist(Image *m, char *rel, void (*fn)(void*, char*, int, uint32_t, uint16_t, uint16_t), void *arg)
{
	Lister l;
	uint32_t s, size;
	int dir;

	if (walk(m, rel, &s, &size, &dir) < 0 || !dir)
		return -1;
	l.m = m;
	l.fn = fn;
	l.arg = arg;
	return eachrec(m, s, size, listed, &l) < 0 ? -1 : 0;
}

/* imagelabel: the volume's identifier, its spaces at the end taken off. */
char*
imagelabel(Image *m)
{
	return m->label;
}

/* tracknum: the audio track a name of an image's root gives as TRACKnn.WAV, whatever its case, or 0. */
int32_t
tracknum(const char *rel)
{
	if (strlen(rel) != 11 || watcomstrnicmp(rel, "TRACK", 5) != 0 || watcomstricmp(rel+7, ".WAV") != 0)
		return 0;
	if (!isdigit((unsigned char)rel[5]) || !isdigit((unsigned char)rel[6]))
		return 0;
	return (rel[5]-'0')*10 + rel[6]-'0';
}

/* The index of the audio track num, or -1. */
static int
audio(Image *m, int32_t num)
{
	int i;

	for (i = 0; i < m->nt; i++)
		if (m->t[i].num == num && m->t[i].audio)
			return i;
	return -1;
}

/* The frames of the track of index i, from its INDEX 01 to the next track's, 0 past UINT32_MAX. */
static uint32_t
frames(Image *m, int i)
{
	uint64_t n;

	n = trackspan(m, i);
	if (i+1 < m->nt && m->t[i+1].file != m->t[i].file)
		n += m->t[i+1].start;
	if (i+1 < m->nt)
		n += m->t[i+1].gap;
	return n > UINT32_MAX ? 0 : n;
}

/* imageaudio: the frames of the audio track num, 0 for no such track. */
uint32_t
imageaudio(Image *m, int32_t num)
{
	int i;

	i = audio(m, num);
	return i < 0 ? 0 : frames(m, i);
}

/* The index of the audio track whose INDEX 01 is the volume's sector s, or -1. */
static int
trackat(Image *m, uint32_t s)
{
	int i;

	for (i = 0; i < m->nt; i++)
		if (m->t[i].audio && m->t[i].lba == s)
			return i;
	return -1;
}

/*
 * The audio track whose frames hold the extent at s of size bytes, a
 * frame for each sector: its index, the extent's first frame of it into
 * *first and its frames into *n, or -1.
 */
static int
inaudio(Image *m, uint32_t s, uint32_t size, uint32_t *first, uint32_t *n)
{
	uint32_t k, f;
	Itrack *t;
	int i;

	k = (size + (uint64_t)Sector-1) / Sector;
	if (k == 0)
		return -1;
	for (i = 0; i < m->nt; i++) {
		t = &m->t[i];
		if (!t->audio || t->lba == UINT32_MAX || s < t->lba)
			continue;
		f = frames(m, i);
		if (s - t->lba < f && k <= f - (s - t->lba)) {
			*first = s - t->lba;
			*n = k;
			return i;
		}
	}
	return -1;
}

/*
 * imagesound: the sound rel names in the image, as a player reads it,
 * its audio track's number into *num, the track's frame it starts at
 * into *first and its frames into *n: 0, or -1 for none. TRACKnn.WAV at
 * the root is the track nn, whole, or on a disc that names its tunes the
 * track TRACKn.WAV starts, n one less, whole, and any other file of the
 * directory whose extent lies in an audio track that extent.
 */
int
imagesound(Image *m, char *rel, int32_t *num, uint32_t *first, uint32_t *n)
{
	char name[16];
	uint32_t s, size, k, f;
	int32_t t;
	int i, dir;

	t = tracknum(rel);
	if (t != 0 && m->named) {
		snprintf(name, sizeof name, "TRACK%d.WAV", (int)t - 1);
		if (walk(m, name, &s, &size, &dir) < 0 || dir || (i = trackat(m, s)) < 0)
			return -1;
		t = m->t[i].num;
	}
	if (t != 0) {
		k = imageaudio(m, t);
		if (k == 0)
			return -1;
		*num = t;
		*first = 0;
		*n = k;
		return 0;
	}
	if (walk(m, rel, &s, &size, &dir) < 0 || dir || (i = inaudio(m, s, size, &f, &k)) < 0)
		return -1;
	*num = m->t[i].num;
	*first = f;
	*n = k;
	return 0;
}

/*
 * n bytes of the big-endian samples of f from its byte at into buf,
 * little-endian, a sample starting at each even byte: the count given,
 * short where the file fails to give them.
 */
static uint32_t
swapped(FILE *f, uint64_t at, uint8_t *buf, uint32_t n)
{
	uint8_t b[Raw];
	uint64_t lo, x;
	uint32_t done, k, j;
	size_t got;

	for (done = 0; done < n; done += k) {
		k = n - done;
		if (k > Raw-2)
			k = Raw-2;
		lo = (at + done) & ~(uint64_t)1;
		if (lo > LONG_MAX || fseek(f, (long)lo, SEEK_SET) != 0)
			break;
		got = fread(b, 1, ((at + done + k + 1) & ~(uint64_t)1) - lo, f);
		for (j = 0; j < k; j++) {
			x = ((at + done + j) ^ 1) - lo;
			if (x >= got)
				return done + j;
			buf[done+j] = b[x];
		}
	}
	return done;
}

/*
 * n bytes of the sound of file i from its byte at, raw CD audio, 4 bytes
 * a sample, into buf: the count given, short where the file fails to
 * give them or a WAVE file's samples end.
 */
static uint32_t
sound(Image *m, int i, uint64_t at, uint8_t *buf, uint32_t n)
{
	if (m->kind[i] == Fwave) {
		if (at >= m->len[i])
			return 0;
		if (n > m->len[i] - at)
			n = m->len[i] - at;
	}
	at += m->base[i];
	if (m->kind[i] == Fbinary && m->swap)
		return swapped(m->f[i], at, buf, n);
	if (at > LONG_MAX || fseek(m->f[i], (long)at, SEEK_SET) != 0)
		return 0;
	return fread(buf, 1, n, m->f[i]);
}

/*
 * imagetrackread: n bytes of the audio track num's sound from its byte
 * at into buf, as a drive plays them: its frames from its INDEX 01 to
 * the next track's, in its file and then in the next where the track
 * ends its own, and the next track's PREGAP as zeros. The count of the
 * track's bytes given, buf zeros past them and where a file fails to
 * give them.
 */
uint32_t
imagetrackread(Image *m, int32_t num, uint64_t at, uint8_t *buf, uint32_t n)
{
	uint64_t len, g0, g1, end, p;
	uint32_t gapat, k, c, got;
	Itrack *t, *next;
	int i, file;

	memset(buf, 0, n);
	i = audio(m, num);
	if (i < 0)
		return 0;
	t = &m->t[i];
	len = (uint64_t)frames(m, i) * Raw;
	/* the next track's PREGAP, from g0 to g1 among the track's bytes */
	g0 = len;
	g1 = len;
	if (i+1 < m->nt) {
		next = &m->t[i+1];
		if (next->idx0 != UINT32_MAX) {
			file = next->idx0file;
			gapat = next->idx0;
		} else {
			file = next->file;
			gapat = next->start;
		}
		if (file == t->file)
			gapat -= t->start;
		else
			gapat += m->nsec[t->file] - t->start;
		g0 = (uint64_t)gapat * Raw;
		g1 = g0 + (uint64_t)next->gap * Raw;
	}
	end = (uint64_t)(m->nsec[t->file] - t->start) * Raw;
	got = 0;
	for (k = 0; k < n && at < len; k += c, at += c) {
		c = n - k;
		if (c > len - at)
			c = len - at;
		if (at >= g0 && at < g1) {
			if (c > g1 - at)
				c = g1 - at;
			got += c;
			continue;
		}
		if (at < g0 && c > g0 - at)
			c = g0 - at;
		p = at < g0 ? at : at - (g1 - g0);
		if (p < end) {
			if (c > end - p)
				c = end - p;
			got += sound(m, t->file, (uint64_t)t->start*Raw + p, buf+k, c);
		} else
			got += sound(m, t->file+1, p - end, buf+k, c);
	}
	return got;
}

/* imageclose: the image's files closed and its memory freed. */
void
imageclose(Image *m)
{
	int i;

	for (i = 0; i < m->nfile; i++) {
		if (m->f[i] != NULL)
			fclose(m->f[i]);
		free(m->path[i]);
	}
	free(m);
}

/* The step from the sample a to the sample b, each given by its 16 bits. */
static uint32_t
step(uint32_t a, uint32_t b)
{
	int32_t x, y;

	x = (int32_t)(a ^ 0x8000) - 0x8000;
	y = (int32_t)(b ^ 0x8000) - 0x8000;
	return x > y ? x - y : y - x;
}

/*
 * Whether the samples of the image's BINARY files are big-endian: their
 * steps from each sample to the next of its channel, summed over Probe
 * frames at Nprobe places of each audio track of a BINARY file, less
 * than a quarter of the same bytes' read little-endian.
 */
static int
bigendian(Image *m)
{
	uint8_t b[Probe*Raw];
	uint64_t le, be, at;
	size_t n, k;
	Itrack *t;
	int i, p;

	le = 0;
	be = 0;
	for (i = 0; i < m->nt; i++) {
		t = &m->t[i];
		if (!t->audio || m->kind[t->file] != Fbinary)
			continue;
		for (p = 0; p < Nprobe; p++) {
			at = (t->start + (uint64_t)trackspan(m, i)*(2*p + 1)/(2*Nprobe)) * Raw;
			if (at > LONG_MAX || fseek(m->f[t->file], (long)at, SEEK_SET) != 0)
				continue;
			n = fread(b, 1, sizeof b, m->f[t->file]);
			for (k = 0; k + 6 <= n; k += 2) {
				le += step(b[k] | b[k+1]<<8, b[k+4] | b[k+5]<<8);
				be += step(b[k]<<8 | b[k+1], b[k+4]<<8 | b[k+5]);
			}
		}
	}
	return be*4 < le;
}

/* The sheet's tracks and the data track's volume checked, its root found: 0, or -1 with err set. */
static int
settle(Image *m, char *err, int nerr)
{
	uint8_t b[Sector];
	uint64_t at, lba[Ntrack];
	uint32_t s, size;
	Itrack *t, *p;
	int i, n, f, dir;

	if (m->nt == 0) {
		seterr(err, nerr, "no track");
		return -1;
	}
	for (i = 0; i < m->nfile; i++)
		if (m->size[i] == 0 || (m->kind[i] == Fbinary && sectors(m->f[i], m->size[i], &m->nsec[i]) < 0)) {
			seterr(err, nerr, "%s: a file without its track or its size", m->path[i]);
			return -1;
		}
	for (i = 0; i < m->nt; i++) {
		t = &m->t[i];
		p = i > 0 ? &m->t[i-1] : NULL;
		if (t->start == UINT32_MAX || t->start > m->nsec[t->file]) {
			seterr(err, nerr, "a track without its INDEX 01 within its file");
			return -1;
		}
		if (p != NULL && t->file == p->file && t->start <= p->start) {
			seterr(err, nerr, "two tracks out of order in one file");
			return -1;
		}
		/*
		 * INDEX 00 lies before INDEX 01 in its file, or in the file of
		 * the track before, past that track's INDEX 01.
		 */
		if (t->idx0 != UINT32_MAX && (t->idx0 > m->nsec[t->idx0file]
		|| (t->idx0file == t->file && t->idx0 > t->start)
		|| (t->idx0file != t->file && (p == NULL || t->idx0file != p->file))
		|| (p != NULL && t->idx0file == p->file && t->idx0 <= p->start))) {
			seterr(err, nerr, "a track's INDEX 00 out of place");
			return -1;
		}
	}
	for (m->data = 0; m->data < m->nt && m->t[m->data].audio; m->data++)
		;
	if (m->data == m->nt) {
		seterr(err, nerr, "no data track");
		return -1;
	}
	m->ndata = trackspan(m, m->data);
	/* each track's INDEX 01 after the files before its own and the PREGAPs' silences, counted from the data track's */
	at = 0;
	f = 0;
	for (i = 0; i < m->nt; i++) {
		t = &m->t[i];
		for (; f < t->file; f++)
			at += m->nsec[f];
		at += t->gap;
		lba[i] = at + t->start;
	}
	for (i = 0; i < m->nt; i++) {
		if (lba[i] < lba[m->data] || lba[i] - lba[m->data] >= UINT32_MAX)
			m->t[i].lba = UINT32_MAX;
		else
			m->t[i].lba = lba[i] - lba[m->data];
	}
	if (datasector(m, Pvd, b) < 0 || b[0] != 1 || memcmp(b+1, "CD001", 5) != 0 || b[6] != 1) {
		seterr(err, nerr, "no ISO 9660 volume in its data track");
		return -1;
	}
	m->root = le32(b+Rootrec+2);
	m->rootsize = le32(b+Rootrec+10);
	if (b[Rootrec] < Nrec+1 || !inside(m, m->root, m->rootsize)) {
		seterr(err, nerr, "a root directory out of the data track");
		return -1;
	}
	memcpy(m->label, b+40, 32);
	for (n = 32; n > 0 && m->label[n-1] == ' '; n--)
		;
	m->label[n] = '\0';
	m->swap = bigendian(m);
	m->named = walk(m, "TRACK1.WAV", &s, &size, &dir) == 0 && !dir && trackat(m, s) >= 0;
	return 0;
}

/*
 * imageopen: the image at path, a cue sheet or an ISO, read as the CD:
 * NULL, with the reason in err, for a file that is neither or a
 * malformed one.
 */
Image*
imageopen(char *path, char *err, int nerr)
{
	uint8_t b[Sector];
	Image *m;
	FILE *f;

	m = calloc(1, sizeof *m);
	if (m == NULL) {
		seterr(err, nerr, "no memory");
		return NULL;
	}
	f = fopen(path, "rb");
	if (f == NULL) {
		seterr(err, nerr, "%s", strerror(errno));
		free(m);
		return NULL;
	}
	if (fseek(f, (long)Pvd*Sector, SEEK_SET) == 0 && fread(b, 1, Sector, f) == Sector && b[0] == 1 && memcmp(b+1, "CD001", 5) == 0) {
		m->path[0] = malloc(strlen(path)+1);
		if (m->path[0] == NULL) {
			fclose(f);
			free(m);
			seterr(err, nerr, "no memory");
			return NULL;
		}
		strcpy(m->path[0], path);
		m->f[0] = f;
		m->size[0] = Sector;
		m->nfile = 1;
		m->t[0].num = 1;
		m->nt = 1;
	} else if (fseek(f, 0, SEEK_SET) == 0 && fread(b, 1, sizeof sync, f) == sizeof sync && memcmp(b, sync, sizeof sync) == 0) {
		seterr(err, nerr, "a raw image of 2352-byte sectors: give its cue sheet");
		fclose(f);
		free(m);
		return NULL;
	} else {
		rewind(f);
		if (cue(m, f, path, err, nerr) < 0) {
			fclose(f);
			imageclose(m);
			return NULL;
		}
		fclose(f);
	}
	if (settle(m, err, nerr) < 0) {
		imageclose(m);
		return NULL;
	}
	return m;
}
