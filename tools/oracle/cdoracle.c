#define _GNU_SOURCE
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
#include "dll.h"
#include "abi.h"
#include "pc.h"

/*
 * cdoracle: the CD as lib386/lib_cd/cdrom.asm drives it through MSCDEX
 * against sys/cd.c, the port's CD, at the interface the game calls.
 *
 *	cdoracle -d dir lba0.elf lba0.sym [image tree tracks]...
 *
 * lba0.elf is a DOS build, as tools/le2elf.py makes it, mapped where it
 * was linked (dll.c), and lba0.sym names its procedures and data
 * (tools/wmap2sym.py of its link map). Each image is a CD's: an ISO, or
 * a cue sheet and its raw files. tree holds the files of its data track
 * as the disc lays them out and tracks its audio tracks as trackNN.wav,
 * cut from it as tools/cdtracks.py cuts them, either - for none: a tree
 * of - leaves the image read as itself alone. In dir it writes discs of
 * its own and the preferences' directory sys/cd.c's side is given.
 *
 * The assembly runs on pc.c's model of the machine the game ran on, its
 * DOS memory and DPMI, the real-mode int 2Fh of DPMI's 0300h and the
 * int 2Fh of the assembly itself answered here by MSCDEX, as Ralf
 * Brown's Interrupt List gives its functions and DOSBox Staging 0.82.2
 * answers them: 1500h (the drives), 150Dh (their letters), 1502h (the
 * copyright file's name in the volume descriptor), 150Fh (a file's
 * directory entry, its name compared in upper case, ";1" and a last dot
 * dropped) and 1510h (the driver's requests: IOCTL input, IOCTL output,
 * READ LONG, PLAY AUDIO, STOP AUDIO, RESUME AUDIO). The drive reads its
 * disc with a cue sheet's reader of its own, apart from sys/cdimage.c,
 * each PREGAP counted into the addresses after it as a drive's table of
 * contents counts it, a track running on past its file into the next,
 * and plays on the oracle's clock, which sys/cd.c's cdtick is given too.
 *
 * sys/cd.c reads the same disc: its image bound as the CD, or its tree
 * then its tracks. Both are called as the game calls them, InitCDR as
 * perso.c does, GetFileCDR and ReadLongCDR as message.c's CopyFileCD_HD
 * does for each file of the disc and for each of the game's voice
 * files, ReadLongCDR again of the disc's first file after GetFileCDR of
 * a file the disc lacks, which must give 0 on both sides,
 * GetLengthTrackCDR as ambiance.c does for tracks 0 to 256,
 * PlayTrackCDR for each audio track and for tracks 0, one past the last
 * and 256, then, while a track plays, PlayTrackCDR of a track the disc
 * lacks, StopCDR and ClearCDR, and ClearCDR as TheEnd does. After
 * InitCDR its result and DriveCDR must be the same, a disc compared
 * whole holding CD_LBA for both and giving a file, and the assembly's
 * LowTrack, HigTrack and LeadOut the disc's, after each GetFileCDR its
 * result, FileCD_Start, FileCD_Sect and FileCD_Size, the bytes read the
 * same up to the file's size, the lengths the same, and the frames a
 * play gives the same, frame for frame, the assembly's those the model
 * reads from where it was asked to play, a track going on or stopping
 * alike. The assembly must keep its calling convention, and give every
 * DPMI 0300h a CX of 0, as MSCDEX takes its arguments in registers.
 * What the port does otherwise on purpose is let pass:
 *
 *	a file of a tree starts at sector 16 in the port, which reads it
 *	relative to that start
 *	the bytes past a file's size the disc's next ones, the port's
 *	zeros, counted: the game copies a voice file's size alone, but
 *	for a size up to 2048 bytes short of a multiple of 65536 its
 *	last block is copied whole, which fails the run
 *	the data track's length, which the port, reading audio tracks
 *	alone, gives as 0
 *	DriveCDR for a CD without CD_LBA, -1 in the assembly and D: in
 *	the port, which reads that CD's other files where they lie
 *	the results of PlayTrackCDR and of ReadLongCDR, compared for 0,
 *	as the game reads them
 *
 * Without images it runs its own discs alone, which is how it runs
 * without the game's files. lba.cue and other.cue are written in dir: a
 * data track with an ISO 9660 of files of 0 to 131073 bytes and of the
 * game's voice files, then audio tracks of patterns, a PREGAP before
 * the first and INDEX 00 gaps of a pattern of their own before the
 * others, the last with a PREGAP before its INDEX 00 too, the copyright
 * file named "CD_LBA." with a file CD_LBA in the root for the first and
 * "OTHER" without it for the second. These run first:
 *
 *	lba.cue		the whole sequence, the port reading the image
 *	lba.cue, its tree
 *			the whole sequence, the port reading lbatree, which
 *			the oracle writes in dir: the disc's files, its tracks
 *			as WAV files, one with a LIST chunk of odd size before
 *			fmt and one with a fact chunk before data, and for the
 *			five tracks after them WAV files the port refuses:
 *			mono, at 22050 Hz, of 8 bits, RIFX, and without data
 *	lbasplit.cue, the port's, lbasplit.cue, the drive's
 *			the whole sequence, the port reading lbasplit.cue, the
 *			same disc over a file a track, which the oracle writes
 *			in dir, track 3's file opening on the last frames of
 *			track 2, and the model lba.cue, then the two swapped
 *	other.cue	InitCDR, the port reading the image
 *	no drive	InitCDR and ClearCDR, MSCDEX without a drive, the
 *			port without a CD bound
 *
 * Then the assembly alone, on its own discs, the model made to fail:
 *
 *	no disc		an empty drive: InitCDR gives 0, DriveCDR -1, and the
 *			drive passed over before its lock
 *	open tray, tray stays open
 *			the game's disc in a tray that refuses to close, or
 *			that stays open once it says it closed, which reads as
 *			an empty drive: the same, and the drive passed over in
 *			fewer than 768 status requests
 *	second drive	the other disc in D: and the game's in E:, where
 *			InitCDR gives 1 and DriveCDR 4
 *	missing file	GetFileCDR of a file the disc lacks, after one it
 *			holds, gives 0 and leaves FileCD_* as they were
 *	audio info	Audio Disk Info failing: InitCDR returns, and
 *			GetLengthTrackCDR(2) gives 0
 *	second track	Audio Track Info of track 3 failing:
 *			GetLengthTrackCDR(2) gives 0
 *	retries		nine requests failing before each answer: the
 *			calls give what they give at once
 *	missing track	PlayTrackCDR of a track the disc lacks sends 20
 *			requests, each call's ten
 *	status after reset, status failing
 *			the drive's status requests failing until a reset, where
 *			InitCDR gives 1 and DriveCDR 3, or for good, where it
 *			passes the drive over: one reset either way
 *	caller's CX	GetCopyrightName, ReadLongCDR and StopCDR entered
 *			with ECX 5555h, which the game's code may leave,
 *			GetCopyrightName after GetFileCDR giving the disc's name
 *	convention	GetCopyrightName and LockCDR keep the calling
 *			convention, called as the game's C would
 *	DOS memory	DPMI's 0100h failing: InitCDR gives 0 and DriveCDR -1
 *	drive letters	DPMI failing InitCDR's call for the drives' letters:
 *			the same, and drive A: named in 0 calls
 *	DPMI failing, drive A
 *			every call of DPMI's 0300h failing, or DriveCDR A:,
 *			which MSCDEX fails: GetFileCDR gives 0, FileCD_* as
 *			they were, ReadLongCDR an error, GetLengthTrackCDR 0,
 *			GetCopyrightName an empty name, and StopCDR tries ten
 *			times
 *
 * Each run goes in a process of its own, ended after 600 seconds, or 10
 * for those on the model made to fail. Each difference is reported, the
 * first 20 of a run in full, and the end is "cdoracle: ok", or
 * "cdoracle: N failed" and status 1.
 */

enum
{
	Sector = 2048,
	Raw = 2352,
	Samples = Raw/4,	/* a frame's, each a 16-bit stereo pair */
	Pad = 150,	/* Red Book's frames before HSG's sector 0 */
	Nfile = 100,
	Ntrack = 99,
	Npath = 1024,
	Timeout = 10,	/* cdrom.asm's TIME_OUT */
	Second = 1000000000,
	Drive = 3,	/* D: */
	Ndrive = 2,
	Nlang = 5,
	Ntext = 15,
	Window = 65536,	/* CopyFileCD_HD's blocks */
};

typedef struct Dtrack Dtrack;
typedef struct Disc Disc;
typedef struct Cdrom Cdrom;
typedef struct Run Run;

/* a track as a cue sheet places it */
struct Dtrack
{
	int	audio;
	int	file;	/* its file's index */
	int	size;	/* bytes of its sectors in the file */
	int	data;	/* where a sector's user data starts in it */
	uint32_t	idx0;	/* its first sector in its file: INDEX 00, or 01 */
	uint32_t	idx1;	/* INDEX 01 in its file */
	uint32_t	gap;	/* a PREGAP's sectors before it, which no file holds */
	uint32_t	base;	/* the disc's sector at its file's sector 0 */
	uint32_t	start;	/* INDEX 01 on the disc */
};

/* a CD's image as the model reads it */
struct Disc
{
	char	*name;	/* for the reports */
	int	nfile;
	FILE	*f[Nfile];
	uint32_t	nsect[Nfile];	/* each file's sectors */
	int	ntrack;	/* tracks 1 to ntrack */
	Dtrack	t[Ntrack+1];
	uint32_t	leadout;
	char	copyright[38];	/* as MSCDEX gives it, to its first space */
	uint32_t	root, rootsize;	/* the root directory's extent and bytes */
	char	**file;	/* every file of the ISO, \DIR\NAME */
	int	nname;
};

/* a drive of the model's MSCDEX */
struct Cdrom
{
	Disc	*disc;	/* NULL: none in the drive */
	int	open;
	int	locked;
	int	playing;	/* a play asked, running to its end on the clock */
	int	paused;	/* a play stopped while it ran */
	uint32_t	from;	/* the play's first sector */
	uint32_t	len;	/* its sectors */
	uint64_t	at;	/* the clock at its start */
	uint32_t	resume;	/* where a paused play goes on */
	int	stuck;	/* its tray: 1 refusing to close, 2 staying open when it says it closed */
	int	failstat;	/* status requests to fail */
	int	resetfixes;	/* a reset ends them */
	int	nlock;	/* locks asked of it */
	int	ncopy;	/* copyright files asked of it */
	int	nreset;	/* resets asked of it */
	int	nstat;	/* status requests asked of it */
};

/* a disc as the two sides read it */
struct Run
{
	char	*label;
	Disc	*disc;	/* the model's, NULL for no drive */
	char	*image;	/* the port's CD, or */
	char	*tree;	/* its tree */
	char	*tracks;	/* and its tracks, NULL for none */
	int	whole;	/* the whole sequence, or InitCDR and ClearCDR */
};

/* sys/cd.c's interface, which lib386/lib_cd/lib_cd.h declares for the game */
int32_t	InitCDR(char*);
void	ClearCDR(void);
void	StopCDR(void);
int32_t	PlayTrackCDR(int32_t);
int32_t	GetLengthTrackCDR(int32_t);
int32_t	GetFileCDR(char*);
int32_t	ReadLongCDR(int32_t, int32_t, void*);
extern int16_t	DriveCDR;
extern int32_t	FileCD_Start;
extern int32_t	FileCD_Sect;
extern int32_t	FileCD_Size;

/* sources/message.c's ListLanguage and ListFileText */
static char *langs[Nlang] = {"EN_", "FR_", "DE_", "SP_", "IT_"};
static char *texts[Ntext] = {
	"sys", "cre", "gam", "000", "001", "002", "003", "004",
	"005", "006", "007", "008", "009", "010", "011",
};

/* the assembly's procedures and data, by lba0.sym */
static struct
{
	uintptr_t	initcdr;
	uintptr_t	clearcdr;
	uintptr_t	stopcdr;
	uintptr_t	playtrackcdr;
	uintptr_t	getlengthtrackcdr;
	uintptr_t	getfilecdr;
	uintptr_t	readlongcdr;
	uintptr_t	getcopyrightname;
	uintptr_t	lockcdr;
	int16_t	*drivecdr;
	int32_t	*start;
	int32_t	*sect;
	int32_t	*size;
	uint8_t	*lowtrack;
	uint8_t	*higtrack;
	uint32_t	*leadout;
} as;

/* the model's machine */
static Cdrom cdrom[Ndrive];
static int ndrive;	/* MSCDEX's drives, 0 without them */
static uint64_t now;	/* the oracle's clock, in ns */
static int failreq;	/* requests that fail before one is answered */
static int failaudio;	/* Audio Disk Info fails */
static int failtrack;	/* Audio Track Info of this track fails */
static int nreq;	/* requests sent to the driver */
static int nadrive;	/* MSCDEX's calls naming drive A:, outside its list */

/* sys/cd.c's play, as sdlcd hands its frames */
static Disc *pdisc;	/* the disc the assembly's play reads, NULL for none */
static uint32_t pfrom;	/* its first sector */
static uint32_t nhanded;	/* frames handed since */
static int64_t firstdiff;	/* the first that differs, -1 for none */

static char *dir;
static char prefdir[Npath];
static uint32_t lowbuf;	/* ReadLongCDR's buffer, in DOS memory as BufSpeak */
static uint8_t cbuf[32*Sector];

static int	sectread(Disc*, uint32_t, int, uint8_t*);

/*
 * sdl.c's and replay.c's, for cd.c. sdlcd compares each frame the port
 * hands with the one the model reads for the assembly's play.
 */
void
sdlcd(const void *buf, int n)
{
	uint8_t f[Raw];

	if (n != Raw)
		fatal("%s: %s: a frame of %d bytes", where, what, n);
	if (pdisc != NULL) {
		sectread(pdisc, pfrom + nhanded, 0, f);
		if (firstdiff < 0 && memcmp(f, buf, Raw) != 0)
			firstdiff = nhanded;
	}
	nhanded++;
}

void
sdlcdstop(void)
{
}

void
sdlpstrack(const void *buf, int n)
{
	USED(buf);
	USED(n);
}

void
sdlpstrackstop(void)
{
}

void
summedcd(uint64_t n, const uint8_t *buf, int nb)
{
	USED(n);
	USED(buf);
	USED(nb);
}

void
summedpstrack(uint64_t n, uint8_t *buf, int nb)
{
	USED(n);
	USED(buf);
	USED(nb);
}

/* Sector a as Red Book gives it: frame, second and minute from the low byte. */
static uint32_t
redbook(uint32_t a)
{
	a += Pad;
	return a%75 | (a/75%60)<<8 | (a/75/60)<<16;
}

/* The track whose part of the disc holds sector a, from its pregap on, or 0. */
static int
trackof(Disc *d, uint32_t a)
{
	Dtrack *t;
	int i;

	for (i = d->ntrack; i >= 1; i--) {
		t = &d->t[i];
		if (a + t->gap >= t->base + t->idx0)
			return i;
	}
	return 0;
}

/*
 * Sector a of d: its raw 2352 bytes, or its 2048 of user data from a
 * data track. A track runs on past its file's end into the next file,
 * up to the next track's INDEX 00. A pregap that no file holds, or the
 * last file's end, reads as zeros. -1 past the lead-out, or for the
 * user data of an audio sector.
 */
static int
sectread(Disc *d, uint32_t a, int cooked, uint8_t *buf)
{
	Dtrack *t;
	FILE *f;
	uint32_t pos;
	int i, k, n;

	n = cooked ? Sector : Raw;
	memset(buf, 0, n);
	if (a >= d->leadout)
		return -1;
	i = trackof(d, a);
	if (i == 0)
		return -1;
	t = &d->t[i];
	if (cooked && t->audio)
		return -1;
	if (a < t->base + t->idx0)
		return 0;
	pos = a - t->base;
	k = t->file;
	if (pos >= d->nsect[k] && k+1 < d->nfile) {
		pos -= d->nsect[k];
		k++;
	}
	if (pos >= d->nsect[k])
		return 0;
	f = d->f[k];
	if (fseek(f, (long)pos*t->size + (cooked ? t->data : 0), SEEK_SET) != 0)
		return 0;
	if (fread(buf, 1, cooked ? Sector : (size_t)t->size, f) == 0)
		memset(buf, 0, n);
	return 0;
}

/* A directory record's name, ";1" and a last dot dropped. */
static void
recname(uint8_t *r, char *name)
{
	int n;
	char *p;

	n = r[32];
	memmove(name, r+33, n);
	name[n] = 0;
	p = strchr(name, ';');
	if (p != NULL)
		*p = 0;
	n = strlen(name);
	if (n > 0 && name[n-1] == '.')
		name[n-1] = 0;
}

static void
upper(char *s)
{
	for (; *s != 0; s++)
		if (*s >= 'a' && *s <= 'z')
			*s -= 'a' - 'A';
}

/* Every file below the directory at ext, of size bytes, its path from prefix. */
static void
listdir(Disc *d, uint32_t ext, uint32_t size, char *prefix, int depth)
{
	uint8_t s[Sector], *r;
	char name[256], path[Npath];
	uint32_t i, off;

	if (depth > 8)
		return;
	for (i = 0; i*Sector < size; i++) {
		if (sectread(d, ext+i, 1, s) < 0)
			return;
		for (off = 0; off + 33 < Sector && s[off] != 0; off += s[off]) {
			r = s + off;
			if (r[32] == 1 && (r[33] == 0 || r[33] == 1))
				continue;
			if (r[25] & 4)
				continue;
			recname(r, name);
			snprintf(path, sizeof path, "%s\\%s", prefix, name);
			if (r[25] & 2) {
				listdir(d, get32(r+2), get32(r+10), path, depth+1);
				continue;
			}
			d->file = realloc(d->file, (d->nname+1) * sizeof d->file[0]);
			if (d->file == NULL)
				fatal("out of memory");
			d->file[d->nname++] = strdup(path);
		}
	}
}

/*
 * The directory record of path on d, into rec: -1 if d lacks it. Each
 * part is compared in upper case, ";1" and a last dot dropped, as
 * MSCDEX compares them.
 */
static int
walk(Disc *d, char *path, uint8_t *rec)
{
	uint8_t s[Sector], *r;
	char part[256], name[256], *p, *q;
	uint32_t ext, size, i, off;
	int n, found;

	ext = d->root;
	size = d->rootsize;
	p = path;
	while (*p == '\\')
		p++;
	for (;;) {
		q = strchr(p, '\\');
		n = q != NULL ? q - p : (int)strlen(p);
		if (n >= (int)sizeof part)
			return -1;
		memmove(part, p, n);
		part[n] = 0;
		upper(part);
		if (n > 1 && part[n-1] == '.')
			part[n-1] = 0;
		found = 0;
		r = NULL;
		for (i = 0; !found && i*Sector < size; i++) {
			if (sectread(d, ext+i, 1, s) < 0)
				return -1;
			for (off = 0; off + 33 < Sector && s[off] != 0; off += s[off]) {
				r = s + off;
				if (r[25] & 4)
					continue;
				recname(r, name);
				upper(name);
				if (strcmp(name, part) == 0) {
					found = 1;
					break;
				}
			}
		}
		if (!found)
			return -1;
		if (q == NULL) {
			memmove(rec, r, r[0]);
			return 0;
		}
		if ((r[25] & 2) == 0)
			return -1;
		ext = get32(r+2);
		size = get32(r+10);
		p = q + 1;
	}
}

/* mm:ss:ff, in sectors */
static uint32_t
msf(char *s)
{
	unsigned m, sec, f;

	if (sscanf(s, "%u:%u:%u", &m, &sec, &f) != 3)
		fatal("not a time: %s", s);
	return (m*60 + sec)*75 + f;
}

/* A cue sheet's tracks and files, with each PREGAP counted into the addresses after it. */
static void
cue(Disc *d, char *path)
{
	FILE *f;
	Dtrack *t;
	char line[Npath], word[32], arg[Npath], file[Npath], *p, *q;
	uint32_t shift, filebase;
	long size[Nfile];
	int k, n, sz;

	f = fopen(path, "r");
	if (f == NULL)
		fatal("%s: cannot open", path);
	t = NULL;
	shift = 0;
	filebase = 0;
	k = -1;
	sz = Raw;
	while (fgets(line, sizeof line, f) != NULL) {
		p = line + strspn(line, " \t");
		if (sscanf(p, "%31s", word) != 1)
			continue;
		upper(word);
		p += strlen(word);
		p += strspn(p, " \t");
		if (strcmp(word, "FILE") == 0) {
			if (*p == '"') {
				q = strchr(p+1, '"');
				if (q == NULL)
					fatal("%s: %s", path, line);
				n = q - (p+1);
				memmove(arg, p+1, n);
				arg[n] = 0;
			} else if (sscanf(p, "%1023s", arg) != 1)
				fatal("%s: %s", path, line);
			if (k >= 0) {
				d->nsect[k] = size[k] / sz;
				filebase += d->nsect[k];
			}
			if (++k == Nfile)
				fatal("%s: more than %d files", path, Nfile);
			q = strrchr(path, '/');
			n = snprintf(file, sizeof file, "%.*s%s", q != NULL ? (int)(q - path + 1) : 0, path, arg);
			if (n < 0 || n >= (int)sizeof file)
				fatal("%s: too long a path", path);
			d->f[k] = fopen(file, "rb");
			if (d->f[k] == NULL || fseek(d->f[k], 0, SEEK_END) != 0 || (size[k] = ftell(d->f[k])) < 0)
				fatal("%s: cannot read", file);
			d->nfile = k + 1;
		} else if (strcmp(word, "TRACK") == 0) {
			if (k < 0 || sscanf(p, "%d %1023s", &n, arg) != 2 || n < 1 || n > Ntrack)
				fatal("%s: %s", path, line);
			upper(arg);
			t = &d->t[n];
			memset(t, 0, sizeof *t);
			d->ntrack = n;
			t->file = k;
			t->audio = strcmp(arg, "AUDIO") == 0;
			t->size = strcmp(arg, "MODE1/2048") == 0 ? Sector : Raw;
			t->data = strcmp(arg, "MODE1/2352") == 0 ? 16 : strcmp(arg, "MODE2/2352") == 0 ? 24 : 0;
			t->idx0 = ~0u;
			sz = t->size;
		} else if (strcmp(word, "PREGAP") == 0 && t != NULL) {
			t->gap = msf(p);
			shift += t->gap;
		} else if (strcmp(word, "INDEX") == 0 && t != NULL) {
			if (sscanf(p, "%d %1023s", &n, arg) != 2)
				fatal("%s: %s", path, line);
			if (n == 0)
				t->idx0 = msf(arg);
			else if (n == 1) {
				t->idx1 = msf(arg);
				if (t->idx0 == ~0u)
					t->idx0 = t->idx1;
				t->base = filebase + shift;
				t->start = t->base + t->idx1;
			}
		}
	}
	fclose(f);
	if (k < 0 || d->ntrack == 0)
		fatal("%s: no track", path);
	d->nsect[k] = size[k] / sz;
	d->leadout = filebase + shift + d->nsect[k];
}

/* An image: an ISO, or a cue sheet's tracks. */
static Disc*
discopen(char *path)
{
	Disc *d;
	FILE *f;
	uint8_t s[Sector];
	char *p;
	long n;

	d = calloc(1, sizeof *d);
	if (d == NULL)
		fatal("out of memory");
	p = strrchr(path, '/');
	d->name = p != NULL ? p + 1 : path;
	f = fopen(path, "rb");
	if (f == NULL)
		fatal("%s: cannot open", path);
	if (fseek(f, 16*Sector, SEEK_SET) == 0 && fread(s, 1, 6, f) == 6 && memcmp(s, "\1CD001", 6) == 0) {
		n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
		if (n < 0)
			fatal("%s: cannot read", path);
		d->nfile = 1;
		d->f[0] = f;
		d->nsect[0] = n / Sector;
		d->ntrack = 1;
		d->t[1].size = Sector;
		d->leadout = d->nsect[0];
	} else {
		fclose(f);
		cue(d, path);
	}
	if (sectread(d, 16, 1, s) < 0 || memcmp(s, "\1CD001", 6) != 0)
		fatal("%s: no ISO 9660 volume descriptor", path);
	memmove(d->copyright, s+702, 37);
	d->copyright[37] = 0;
	d->copyright[strcspn(d->copyright, " ")] = 0;
	d->root = get32(s+156+2);
	d->rootsize = get32(s+156+10);
	listdir(d, d->root, d->rootsize, "", 0);
	return d;
}

static Cdrom*
unit(uint32_t drive)
{
	drive &= 0xffff;
	if (drive < Drive || drive >= (uint32_t)(Drive + ndrive))
		return NULL;
	return &cdrom[drive - Drive];
}

static int
isplaying(Cdrom *c)
{
	if (c->playing && (now - c->at) * 75 / Second >= c->len)
		c->playing = 0;
	return c->playing;
}

/* Where the head is: in the play, or where it stopped. */
static uint32_t
head(Cdrom *c)
{
	if (isplaying(c))
		return c->from + (uint32_t)((now - c->at) * 75 / Second);
	return c->paused ? c->resume : c->from;
}

static int
play(Cdrom *c, uint32_t from, uint32_t len)
{
	int i;

	if (c->disc == NULL || from >= c->disc->leadout || len > c->disc->leadout - from)
		return 0;
	i = trackof(c->disc, from);
	if (i == 0 || !c->disc->t[i].audio)
		return 0;
	c->playing = 1;
	c->paused = 0;
	c->from = from;
	c->len = len;
	c->at = now;
	return 1;
}

/* IOCTL input: the control block at b. 0 if the drive answers. */
static int
ioctlin(Cdrom *c, uint8_t *b)
{
	Disc *d;
	uint32_t a, s, p;
	int i;

	d = c->open ? NULL : c->disc;
	switch (b[0]) {
	case 1:
		a = head(c);
		if (b[1] == 0)
			put32(b+2, a);
		else
			put32(b+2, redbook(a));
		return 0;
	case 4:
		for (i = 0; i < 4; i++) {
			b[1+2*i] = i;
			b[2+2*i] = 0xff;
		}
		return 0;
	case 6:
		c->nstat++;
		if (c->failstat > 0) {
			c->failstat--;
			return -1;
		}
		s = 1<<2 | 1<<4 | 1<<8 | 1<<9;
		if (c->open)
			s |= 1<<0;
		if (!c->locked)
			s |= 1<<1;
		if (isplaying(c))
			s |= 1<<10;
		if (d == NULL)
			s |= 1<<11;
		put32(b+1, s);
		return 0;
	case 7:
		put16(b+2, b[1] == 0 ? Sector : Raw);
		return 0;
	case 8:
		put32(b+1, d != NULL ? d->leadout : 0);
		return 0;
	case 9:
		b[1] = 1;
		return 0;
	case 10:
		if (d == NULL || failaudio) {
			b[1] = 1;
			b[2] = 1;
			put32(b+3, 0);
			return -1;
		}
		b[1] = 1;
		b[2] = d->ntrack;
		put32(b+3, redbook(d->leadout));
		return 0;
	case 11:
		i = b[1];
		if (d == NULL || i < 1 || i > d->ntrack || i == failtrack) {
			memset(b+2, 0, 5);
			return -1;
		}
		put32(b+2, redbook(d->t[i].start));
		b[6] = d->t[i].audio ? 0 : 0x40;
		return 0;
	case 12:
		memset(b+1, 0, 10);
		if (d == NULL)
			return -1;
		a = head(c);
		i = trackof(d, a);
		if (i == 0)
			i = 1;
		p = a >= d->t[i].start ? a - d->t[i].start : 0;
		b[1] = d->t[i].audio ? 0x01 : 0x41;
		b[2] = i/10<<4 | i%10;
		b[3] = 1;
		b[4] = p/75/60;
		b[5] = p/75%60;
		b[6] = p%75;
		s = redbook(a);
		b[8] = s >> 16;
		b[9] = s >> 8;
		b[10] = s;
		return 0;
	case 15:
		memset(b+1, 0, 10);
		b[1] = c->paused;
		if (c->paused) {
			s = redbook(c->resume);
			b[3] = s >> 16;
			b[4] = s >> 8;
			b[5] = s;
			s = redbook(c->from + c->len);
			b[7] = s >> 16;
			b[8] = s >> 8;
			b[9] = s;
		}
		return 0;
	}
	return 3;
}

/* IOCTL output: the control block at b. 0 if the drive answers. */
static int
ioctlout(Cdrom *c, uint8_t *b)
{
	switch (b[0]) {
	case 0:
		c->open = 1;
		return 0;
	case 1:
		c->locked = b[1] != 0;
		c->nlock += c->locked;
		return 0;
	case 2:
		c->nreset++;
		if (c->resetfixes)
			c->failstat = 0;
		c->playing = 0;
		c->paused = 0;
		return 0;
	case 3:
		return 0;
	case 5:
		if (c->stuck == 1)
			return 2;
		if (c->stuck == 0)
			c->open = 0;
		return 0;
	}
	return 3;
}

/* A device request at rq, as MSCDEX's driver answers it: its status. */
static uint32_t
request(Cdrom *c, uint8_t *rq)
{
	Disc *d;
	uint8_t s[Raw], *b;
	uint32_t from, n, i;
	int r;

	nreq++;
	if (failreq > 0) {
		failreq--;
		return 0x8000 | 2;
	}
	d = c->open ? NULL : c->disc;
	r = 0;
	switch (rq[2]) {
	case 3:
		r = ioctlin(c, lowmem(rq[16] | rq[17]<<8, rq[14] | rq[15]<<8, 11));
		break;
	case 12:
		r = ioctlout(c, lowmem(rq[16] | rq[17]<<8, rq[14] | rq[15]<<8, 9));
		break;
	case 13:
	case 14:
		break;
	case 128:
		from = get32(rq+20);
		if (rq[13] == 1)
			from = (from>>16 & 0xff)*4500 + (from>>8 & 0xff)*75 + (from & 0xff) - Pad;
		n = rq[18] | rq[19]<<8;
		b = lowmem(rq[16] | rq[17]<<8, rq[14] | rq[15]<<8, n*(rq[24] ? Raw : Sector));
		if (d == NULL) {
			r = -1;
			break;
		}
		for (i = 0; i < n; i++) {
			if (sectread(d, from+i, rq[24] == 0, s) < 0) {
				r = -1;
				break;
			}
			memmove(b + i*(rq[24] ? Raw : Sector), s, rq[24] ? Raw : Sector);
		}
		break;
	case 132:
		from = get32(rq+14);
		if (rq[13] == 1)
			from = (from>>16 & 0xff)*4500 + (from>>8 & 0xff)*75 + (from & 0xff) - Pad;
		n = get32(rq+18);
		if (c->paused && from == c->resume)
			r = play(c, c->resume, c->from + c->len - c->resume) ? 0 : -1;
		else
			r = play(c, from, n) ? 0 : -1;
		break;
	case 133:
		if (isplaying(c)) {
			c->resume = head(c);
			c->paused = 1;
		} else
			c->paused = 0;
		c->playing = 0;
		break;
	case 136:
		if (!c->paused)
			r = -1;
		else
			r = play(c, c->resume, c->from + c->len - c->resume) ? 0 : -1;
		break;
	default:
		r = 3;
		break;
	}
	if (r < 0)
		r = 0x8000 | 12;
	else if (r > 0)
		r = 0x8000 | r;
	else
		r = 0x100;
	if (isplaying(c))
		r |= 0x200;
	return r;
}

static void
err(Cpu *r, int code)
{
	setlo(&r->eax, code);
	r->flags |= 1;
}

/* MSCDEX: an int 2Fh, function AX, its addresses in DOS memory. */
static void
mscdex(Cpu *r)
{
	Cdrom *c;
	uint8_t rec[256], *b;
	char path[256];
	int i;

	r->flags &= ~1;
	if ((r->ecx & 0xff) == 0 && ((r->eax & 0xffff) == 0x1502 || (r->eax & 0xffff) == 0x150f || (r->eax & 0xffff) == 0x1510))
		nadrive++;
	switch (r->eax & 0xffff) {
	case 0x1500:
		if (ndrive > 0) {
			setlo(&r->ebx, ndrive);
			setlo(&r->ecx, Drive);
		}
		return;
	case 0x150d:
		b = lowmem(r->es, r->ebx, ndrive);
		for (i = 0; i < ndrive; i++)
			b[i] = Drive + i;
		return;
	case 0x1502:
		c = unit(r->ecx);
		if (c != NULL)
			c->ncopy++;
		if (c == NULL || c->disc == NULL) {
			err(r, 15);
			return;
		}
		if (c->open) {
			err(r, 21);
			return;
		}
		b = lowmem(r->es, r->ebx, 38);
		strcpy((char*)b, c->disc->copyright);
		return;
	case 0x150f:
		c = unit(r->ecx & 0xff);
		if (c == NULL || c->disc == NULL) {
			err(r, 15);
			return;
		}
		if (c->open) {
			err(r, 21);
			return;
		}
		b = lowmem(r->es, r->ebx, sizeof path);
		memmove(path, b, sizeof path - 1);
		path[sizeof path - 1] = 0;
		if (walk(c->disc, path, rec) < 0) {
			err(r, 2);
			return;
		}
		b = lowmem(r->esi, r->edi, 0x18 + 40);
		if (r->ecx & 0x100) {
			memset(b, 0, 0x18 + 40);
			b[0] = rec[1];
			memmove(b+1, rec+2, 4);
			b[6] = 8;
			memmove(b+7, rec+10, 4);
			memmove(b+11, rec+18, 6);
			b[0x11] = rec[24];
			b[0x12] = rec[25];
			b[0x13] = rec[26];
			b[0x14] = rec[27];
			memmove(b+0x15, rec+28, 2);
			b[0x17] = rec[32];
			memmove(b+0x18, rec+33, rec[32] < 38 ? rec[32] : 38);
		} else
			memmove(b, rec, rec[0]);
		setlo(&r->eax, 1);
		return;
	case 0x1510:
		c = unit(r->ecx);
		if (c == NULL) {
			err(r, 15);
			return;
		}
		b = lowmem(r->es, r->ebx, 27);
		b[1] = (r->ecx & 0xffff) - Drive;
		put16(b+3, request(c, b));
		return;
	}
	err(r, 1);
}

/* The clock moved on by ns, a quarter second at a time, sys/cd.c handing what it passes. */
static void
follow(uint64_t ns)
{
	uint64_t end;

	end = now + ns;
	while (now < end) {
		now += Second/4;
		cdtick(now);
	}
}

/* The two sides' FileCD_*: the start compared on an image alone. */
static void
samefile(Run *r, int32_t a, int32_t p)
{
	if (a != p)
		fail("the assembly's %d, ours %d", a, p);
	if (*as.size != FileCD_Size || *as.sect != FileCD_Sect)
		fail("FileCD_Size and FileCD_Sect: the assembly's %d and %d, ours %d and %d",
			*as.size, *as.sect, FileCD_Size, FileCD_Sect);
	if (r->tree == NULL && *as.start != FileCD_Start)
		fail("FileCD_Start: the assembly's %d, ours %d", *as.start, FileCD_Start);
}

/*
 * ReadLongCDR of k sectors from sector off of the file as each side
 * places it, the bytes up to its size compared, those past it counted.
 */
static int
samereads(int32_t sa, int32_t sc, int32_t off, int32_t k, int32_t size, int voice, int *past)
{
	int32_t a, p, n;
	uint8_t *b;

	b = (uint8_t*)(uintptr_t)lowbuf;
	memset(b, 0xa5, k*Sector);
	memset(cbuf, 0x5a, k*Sector);
	a = acall(as.readlongcdr, 3, sa+off, k, (int32_t)lowbuf);
	p = ReadLongCDR(sc+off, k, cbuf);
	if ((a != 0) != (p != 0)) {
		fail("ReadLongCDR(%d, %d): the assembly's %d, ours %d", off, k, a, p);
		return -1;
	}
	if (a != 0)
		return 0;
	n = size - off*Sector;
	if (n > k*Sector)
		n = k*Sector;
	if (n > 0 && memcmp(b, cbuf, n) != 0) {
		fail("ReadLongCDR(%d, %d): the bytes differ", off, k);
		return -1;
	}
	if (n < 0)
		n = 0;
	if (memcmp(b+n, cbuf+n, k*Sector - n) != 0) {
		(*past)++;
		if (voice && k == 32)
			fail("ReadLongCDR(%d, %d): a voice file of %d bytes, past its end in a block copied whole", off, k, size);
	}
	return 0;
}

/* GetFileCDR of name, then, if the disc holds it, ReadLongCDR as CopyFileCD_HD reads it. */
static void
samefilecopy(Run *r, char *name, int voice, int *nfile, int *past)
{
	uint8_t rec[256];
	int32_t a, p, sa, sc, size, off, n;

	snprintf(what, sizeof what, "GetFileCDR(%s)", name);
	a = acall(as.getfilecdr, 1, (int32_t)(uintptr_t)name);
	p = GetFileCDR(name);
	samefile(r, a, p);
	if (walk(r->disc, name, rec) < 0 || a == 0 || p == 0)
		return;
	(*nfile)++;
	sa = *as.start;
	sc = FileCD_Start;
	size = FileCD_Size;
	n = FileCD_Sect;
	for (off = 0; off <= n-32; off += 32)
		if (samereads(sa, sc, off, 32, size, voice, past) < 0)
			return;
	if (n - off > 0)
		samereads(sa, sc, off, n - off, size, voice, past);
}

/*
 * GetFileCDR of name, then of a file the disc lacks: ReadLongCDR reads
 * name's first sector, the last file found, on both.
 */
static void
samelast(Run *r, char *name)
{
	int32_t a, p, sa, sc, size;
	int past;

	snprintf(what, sizeof what, "GetFileCDR(%s)", name);
	a = acall(as.getfilecdr, 1, (int32_t)(uintptr_t)name);
	p = GetFileCDR(name);
	samefile(r, a, p);
	if (a == 0 || p == 0)
		return;
	sa = *as.start;
	sc = FileCD_Start;
	size = FileCD_Size;
	snprintf(what, sizeof what, "GetFileCDR(\\LBA\\VOX\\IT_sys.VOX) after %s", name);
	a = acall(as.getfilecdr, 1, (int32_t)(uintptr_t)"\\LBA\\VOX\\IT_sys.VOX");
	p = GetFileCDR("\\LBA\\VOX\\IT_sys.VOX");
	if (a != 0 || p != 0) {
		fail("the assembly's %d, ours %d, not 0", a, p);
		return;
	}
	past = 0;
	samereads(sa, sc, 0, 1, size, 0, &past);
}

/* PlayTrackCDR(t) on both, the frames compared to the play's end. */
static void
sameplay(Run *r, int t, uint32_t *frames)
{
	Cdrom *c;
	int32_t a, p;

	c = &cdrom[0];
	snprintf(what, sizeof what, "PlayTrackCDR(%d)", t);
	now += Second;
	cdtick(now);
	a = acall(as.playtrackcdr, 1, t);
	p = PlayTrackCDR(t);
	if ((a != 0) != (p != 0))
		fail("the assembly's %d, ours %d", a, p);
	nhanded = 0;
	firstdiff = -1;
	if (!isplaying(c)) {
		pdisc = NULL;
		follow(Second);
		if (nhanded != 0)
			fail("the assembly plays nothing, ours %u frames", nhanded);
		return;
	}
	pdisc = r->disc;
	pfrom = c->from;
	follow((uint64_t)c->len * Second / 75 + Second);
	if (nhanded != c->len)
		fail("the assembly plays %u frames, ours %u", c->len, nhanded);
	if (firstdiff >= 0)
		fail("frame %lld differs", (long long)firstdiff);
	*frames += nhanded;
	pdisc = NULL;
}

/*
 * A track asked for while another plays, which the disc lacks, then
 * StopCDR, then ClearCDR while it plays: both go on, then stop, alike.
 */
static void
samestops(int t, int missing)
{
	Cdrom *c;
	int32_t a, p;

	c = &cdrom[0];
	snprintf(what, sizeof what, "PlayTrackCDR(%d)", t);
	now += Second;
	cdtick(now);
	acall(as.playtrackcdr, 1, t);
	PlayTrackCDR(t);
	pdisc = NULL;
	follow(Second);
	snprintf(what, sizeof what, "PlayTrackCDR(%d) while track %d plays", missing, t);
	a = acall(as.playtrackcdr, 1, missing);
	p = PlayTrackCDR(missing);
	if ((a != 0) != (p != 0))
		fail("the assembly's %d, ours %d", a, p);
	nhanded = 0;
	follow(Second);
	if (!isplaying(c) || nhanded == 0)
		fail("the assembly's track %s, ours %u frames", isplaying(c) ? "plays" : "stopped", nhanded);
	snprintf(what, sizeof what, "StopCDR while track %d plays", t);
	acall(as.stopcdr, 0);
	StopCDR();
	nhanded = 0;
	follow(Second);
	if (isplaying(c) || nhanded != 0)
		fail("the assembly's track %s, ours %u frames", isplaying(c) ? "plays" : "stopped", nhanded);
	snprintf(what, sizeof what, "ClearCDR while track %d plays", t);
	acall(as.playtrackcdr, 1, t);
	PlayTrackCDR(t);
	follow(Second);
	acall(as.clearcdr, 0);
	ClearCDR();
	nhanded = 0;
	follow(Second);
	if (isplaying(c) || nhanded != 0)
		fail("the assembly's track %s, ours %u frames", isplaying(c) ? "plays" : "stopped", nhanded);
}

/* sys/cd.c's CD: the image, or the tree then the tracks, or none. */
static void
portcd(Run *r)
{
	char e[256];

	setpref(prefdir);
	if (r->image != NULL && bindpath(1, "", r->image, e, sizeof e) < 0)
		fatal("%s: %s", r->image, e);
	if (r->tree != NULL && bindpath(1, "", r->tree, e, sizeof e) < 0)
		fatal("%s: %s", r->tree, e);
	if (r->tracks != NULL && r->tree != NULL && strcmp(r->tracks, r->tree) != 0
	&& bindpath(1, "", r->tracks, e, sizeof e) < 0)
		fatal("%s: %s", r->tracks, e);
}

/* The game's calls on both sides, in this process. */
static void
compare(Run *r)
{
	Disc *d;
	char name[Npath];
	int32_t a, p, t;
	uint32_t frames;
	int i, j, nfile, past, ntrack, audio, kept;

	d = r->disc;
	memset(cdrom, 0, sizeof cdrom);
	ndrive = r->disc != NULL || r->image != NULL ? 1 : 0;
	cdrom[0].disc = d;
	portcd(r);
	snprintf(what, sizeof what, "InitCDR(CD_LBA)");
	a = acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
	p = InitCDR("CD_LBA");
	/* a CD without CD_LBA keeps D: in the port, which reads its other files where they lie */
	kept = a == 0 && p == 0 && *as.drivecdr == -1 && DriveCDR == Drive;
	if ((a != 0) != (p != 0) || (*as.drivecdr != DriveCDR && !kept))
		fail("the assembly's %d and DriveCDR %d, ours %d and %d", a, *as.drivecdr, p, DriveCDR);
	if (r->whole && r->disc != NULL && (a == 0 || p == 0))
		fail("the assembly's %d, ours %d, on a disc to compare whole, which holds CD_LBA", a, p);
	if (a != 0 && d != NULL && (*as.lowtrack != 1 || *as.higtrack != d->ntrack || *as.leadout != redbook(d->leadout)))
		fail("LowTrack %d, HigTrack %d and LeadOut %06x, the disc's 1, %d and %06x",
			*as.lowtrack, *as.higtrack, *as.leadout, d->ntrack, redbook(d->leadout));
	nfile = 0;
	past = 0;
	frames = 0;
	ntrack = 0;
	if (r->whole && a != 0) {
		for (i = 0; i < d->nname; i++)
			samefilecopy(r, d->file[i], 0, &nfile, &past);
		for (i = 0; i < Nlang; i++)
			for (j = 0; j < Ntext; j++) {
				snprintf(name, sizeof name, "\\LBA\\VOX\\%s%s.VOX", langs[i], texts[j]);
				samefilecopy(r, name, 1, &nfile, &past);
			}
		if (d->nname > 0)
			samelast(r, d->file[0]);
		for (t = 0; t <= 256; t++) {
			snprintf(what, sizeof what, "GetLengthTrackCDR(%d)", t);
			a = acall(as.getlengthtrackcdr, 1, t);
			p = GetLengthTrackCDR(t);
			if (t >= 1 && t <= d->ntrack && !d->t[t].audio)
				continue;
			if (a != p)
				fail("the assembly's %d, ours %d", a, p);
		}
		audio = 0;
		for (t = 1; t <= d->ntrack; t++)
			if (d->t[t].audio) {
				sameplay(r, t, &frames);
				ntrack++;
				if (audio == 0)
					audio = t;
			}
		sameplay(r, 0, &frames);
		sameplay(r, d->ntrack+1, &frames);
		sameplay(r, 256, &frames);
		if (audio != 0)
			samestops(audio, d->ntrack+1);
		if (nfile == 0)
			fail("no file compared on a disc to compare whole");
	}
	snprintf(what, sizeof what, "ClearCDR");
	acall(as.clearcdr, 0);
	ClearCDR();
	if (ncx != 0)
		fail("%d DPMI 0300h calls given a CX other than 0, the last %u", ncx, cxlast);
	if (nfail == 0) {
		if (r->whole && r->disc != NULL)
			printf("%s: %d files, %d tracks, %u frames, %d reads past an end, the same\n",
				r->label, nfile, ntrack, frames, past);
		else if (kept)
			printf("%s: InitCDR %d, DriveCDR %d, ours %d as it keeps D:, the same\n", r->label, p,
				*as.drivecdr, DriveCDR);
		else
			printf("%s: InitCDR %d, DriveCDR %d, the same\n", r->label, p, DriveCDR);
	}
}

static void
comparerun(void *v)
{
	compare(v);
}

/* The assembly alone, on a model told to fail. */
typedef struct Fault Fault;

struct Fault
{
	char	*name;
	Disc	*lba;
	Disc	*other;
};

/* MSCDEX's drives for the assembly alone: d0 in D:, d1 in E: if any. */
static void
initasm(Disc *d0, Disc *d1)
{
	memset(cdrom, 0, sizeof cdrom);
	cdrom[0].disc = d0;
	cdrom[1].disc = d1;
	ndrive = d1 != NULL ? 2 : 1;
}

static void
faultrun(void *v)
{
	Fault *f;
	int32_t r, s0, s1, s2, n, a[3];

	f = v;
	if (strcmp(f->name, "no disc") == 0 || strcmp(f->name, "open tray") == 0
	|| strcmp(f->name, "tray stays open") == 0) {
		initasm(f->name[0] == 'n' ? NULL : f->lba, NULL);
		if (f->name[0] != 'n') {
			cdrom[0].open = 1;
			cdrom[0].stuck = f->name[0] == 'o' ? 1 : 2;
		}
		snprintf(what, sizeof what, "InitCDR(CD_LBA)");
		r = acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		if (r != 0 || *as.drivecdr != -1)
			fail("gives %d and DriveCDR %d, not 0 and -1", r, *as.drivecdr);
		if (cdrom[0].nlock != 0 || cdrom[0].ncopy != 0)
			fail("locks the drive %d times and asks its copyright file %d, not 0 and 0",
				cdrom[0].nlock, cdrom[0].ncopy);
		if (f->name[0] != 'n' && cdrom[0].nstat >= 768)
			fail("asks the drive its status %d times, waiting for a disc in an open tray",
				cdrom[0].nstat);
	} else if (strcmp(f->name, "status after reset") == 0 || strcmp(f->name, "status failing") == 0) {
		initasm(f->lba, NULL);
		cdrom[0].failstat = 1000000;
		cdrom[0].resetfixes = strcmp(f->name, "status after reset") == 0;
		snprintf(what, sizeof what, "InitCDR(CD_LBA)");
		r = acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		n = cdrom[0].resetfixes ? Drive : -1;
		if ((r != 0) != cdrom[0].resetfixes || *as.drivecdr != n || cdrom[0].nreset != 1)
			fail("gives %d and DriveCDR %d after %d resets, not %d and %d after 1", r, *as.drivecdr,
				cdrom[0].nreset, cdrom[0].resetfixes, n);
	} else if (strcmp(f->name, "DOS memory") == 0) {
		initasm(f->lba, NULL);
		dpmifail = 0x0100;
		ndpmifail = -1;
		snprintf(what, sizeof what, "InitCDR(CD_LBA)");
		r = acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		if (r != 0 || *as.drivecdr != -1)
			fail("gives %d and DriveCDR %d, not 0 and -1", r, *as.drivecdr);
	} else if (strcmp(f->name, "drive letters") == 0) {
		initasm(f->lba, NULL);
		dpmifail = 0x0300;
		ndpmifail = 1;
		snprintf(what, sizeof what, "InitCDR(CD_LBA)");
		r = acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		if (r != 0 || *as.drivecdr != -1 || nadrive != 0)
			fail("gives %d and DriveCDR %d, drive A: named %d times, not 0, -1 and 0", r,
				*as.drivecdr, nadrive);
	} else if (strcmp(f->name, "DPMI failing") == 0 || strcmp(f->name, "drive A") == 0) {
		initasm(f->lba, NULL);
		acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		acall(as.getfilecdr, 1, (int32_t)(uintptr_t)"\\LBA\\VOX\\EN_sys.VOX");
		s0 = *as.start;
		s1 = *as.sect;
		s2 = *as.size;
		if (f->name[0] == 'D') {
			dpmifail = 0x0300;
			ndpmifail = -1;
		} else
			*as.drivecdr = 0;
		snprintf(what, sizeof what, "GetFileCDR(\\LBA\\VOX\\EN_001.VOX)");
		r = acall(as.getfilecdr, 1, (int32_t)(uintptr_t)"\\LBA\\VOX\\EN_001.VOX");
		if (r != 0 || *as.start != s0 || *as.sect != s1 || *as.size != s2)
			fail("gives %d and FileCD_* %d %d %d, not 0 and %d %d %d", r,
				*as.start, *as.sect, *as.size, s0, s1, s2);
		snprintf(what, sizeof what, "ReadLongCDR");
		r = acall(as.readlongcdr, 3, s0, 1, (int32_t)lowbuf);
		if (r == 0)
			fail("gives 0");
		snprintf(what, sizeof what, "GetLengthTrackCDR(2)");
		r = acall(as.getlengthtrackcdr, 1, 2);
		if (r != 0)
			fail("gives %d, not 0", r);
		snprintf(what, sizeof what, "GetCopyrightName");
		r = acall(as.getcopyrightname, 0);
		if (*(uint8_t*)(uintptr_t)r != 0)
			fail("gives a name of first byte %02xh, not an empty one", *(uint8_t*)(uintptr_t)r);
		snprintf(what, sizeof what, "StopCDR");
		n = nadrive;
		if (f->name[0] == 'D')
			ndpmifail = Timeout;
		acall(as.stopcdr, 0);
		if (f->name[0] == 'D' ? ndpmifail != 0 : nadrive - n != Timeout)
			fail("sends %d requests, not %d", f->name[0] == 'D' ? Timeout - ndpmifail : nadrive - n, Timeout);
	} else if (strcmp(f->name, "second drive") == 0) {
		initasm(f->other, f->lba);
		snprintf(what, sizeof what, "InitCDR(CD_LBA)");
		r = acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		if (r == 0 || *as.drivecdr != Drive+1)
			fail("gives %d and DriveCDR %d, not 1 and %d", r, *as.drivecdr, Drive+1);
	} else if (strcmp(f->name, "missing file") == 0) {
		initasm(f->lba, NULL);
		acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		snprintf(what, sizeof what, "GetFileCDR(\\LBA\\VOX\\EN_sys.VOX)");
		acall(as.getfilecdr, 1, (int32_t)(uintptr_t)"\\LBA\\VOX\\EN_sys.VOX");
		s0 = *as.start;
		s1 = *as.sect;
		s2 = *as.size;
		snprintf(what, sizeof what, "GetFileCDR(\\LBA\\VOX\\IT_sys.VOX)");
		r = acall(as.getfilecdr, 1, (int32_t)(uintptr_t)"\\LBA\\VOX\\IT_sys.VOX");
		if (r != 0 || *as.start != s0 || *as.sect != s1 || *as.size != s2)
			fail("gives %d and FileCD_* %d %d %d, not 0 and %d %d %d", r,
				*as.start, *as.sect, *as.size, s0, s1, s2);
	} else if (strcmp(f->name, "audio info") == 0) {
		initasm(f->lba, NULL);
		failaudio = 1;
		snprintf(what, sizeof what, "InitCDR(CD_LBA)");
		r = acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		if (r == 0 || *as.drivecdr != Drive)
			fail("gives %d and DriveCDR %d, not 1 and %d", r, *as.drivecdr, Drive);
		snprintf(what, sizeof what, "GetLengthTrackCDR(2)");
		r = acall(as.getlengthtrackcdr, 1, 2);
		if (r != 0)
			fail("gives %d, not 0", r);
	} else if (strcmp(f->name, "second track") == 0) {
		initasm(f->lba, NULL);
		acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		failtrack = 3;
		snprintf(what, sizeof what, "GetLengthTrackCDR(2)");
		r = acall(as.getlengthtrackcdr, 1, 2);
		if (r != 0)
			fail("gives %d, not 0", r);
	} else if (strcmp(f->name, "retries") == 0) {
		initasm(f->lba, NULL);
		failreq = Timeout - 1;
		snprintf(what, sizeof what, "InitCDR(CD_LBA)");
		r = acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		if (r == 0)
			fail("gives 0");
		snprintf(what, sizeof what, "GetLengthTrackCDR(2)");
		n = acall(as.getlengthtrackcdr, 1, 2);
		failreq = Timeout - 1;
		r = acall(as.getlengthtrackcdr, 1, 2);
		if (r != n || n != (int32_t)(f->lba->t[3].start - f->lba->t[2].start))
			fail("gives %d after nine failures, %d at once, the disc's %d", r, n,
				f->lba->t[3].start - f->lba->t[2].start);
		snprintf(what, sizeof what, "PlayTrackCDR(2)");
		failreq = Timeout - 1;
		acall(as.playtrackcdr, 1, 2);
		if (!isplaying(&cdrom[0]) || cdrom[0].from != f->lba->t[2].start)
			fail("plays %s", isplaying(&cdrom[0]) ? "elsewhere" : "nothing");
	} else if (strcmp(f->name, "missing track") == 0) {
		initasm(f->lba, NULL);
		acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		nreq = 0;
		snprintf(what, sizeof what, "PlayTrackCDR(%d)", f->lba->ntrack+1);
		acall(as.playtrackcdr, 1, f->lba->ntrack+1);
		if (nreq != 2*Timeout)
			fail("sends %d requests, not %d", nreq, 2*Timeout);
	} else if (strcmp(f->name, "caller's CX") == 0) {
		initasm(f->lba, NULL);
		acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		a[0] = (int32_t)(uintptr_t)"\\LBA\\VOX\\EN_sys.VOX";
		acall(as.getfilecdr, 1, a[0]);
		snprintf(what, sizeof what, "GetCopyrightName");
		r = cxcall(as.getcopyrightname, 0, a, 0x5555);
		cxnone();
		if (strcmp((char*)(uintptr_t)r, f->lba->copyright) != 0)
			fail("gives \"%.38s\", not the disc's \"%s\"", (char*)(uintptr_t)r, f->lba->copyright);
		snprintf(what, sizeof what, "ReadLongCDR");
		a[0] = *as.start;
		a[1] = *as.sect;
		a[2] = lowbuf;
		cxcall(as.readlongcdr, 3, a, 0x5555);
		cxnone();
		snprintf(what, sizeof what, "StopCDR");
		cxcall(as.stopcdr, 0, a, 0x5555);
		cxnone();
	} else if (strcmp(f->name, "convention") == 0) {
		initasm(f->lba, NULL);
		acall(as.initcdr, 1, (int32_t)(uintptr_t)"CD_LBA");
		snprintf(what, sizeof what, "GetCopyrightName");
		acall(as.getcopyrightname, 0);
		snprintf(what, sizeof what, "LockCDR");
		acall(as.lockcdr, 0);
	}
	if (ncx != 0)
		fail("%d DPMI 0300h calls given a CX other than 0, the last %u", ncx, cxlast);
	if (nfail == 0)
		printf("%s: as it should\n", f->name);
}

/* The time of sector a in a cue sheet: mm:ss:ff. */
static char*
cuetime(uint32_t a, char *buf)
{
	sprintf(buf, "%02u:%02u:%02u", a/75/60, a/75%60, a%75);
	return buf;
}

/*
 * A WAV file of n frames of d from sector from, as tools/cdtracks.py
 * writes a track, its chunks laid out by kind: 'l' a LIST chunk of odd
 * size before fmt, 'f' a fact chunk before data, or one the port must
 * refuse, 'm' mono, 'r' at 22050 Hz, 'b' of 8 bits, 'x' RIFX, 'd'
 * without data.
 */
static void
putwav(char *path, Disc *d, uint32_t from, uint32_t n, int kind)
{
	uint8_t h[64], f[Raw];
	FILE *w;
	uint32_t i, len;

	w = fopen(path, "wb");
	if (w == NULL)
		fatal("%s: cannot write", path);
	len = 0;
	memmove(h, kind == 'x' ? "RIFX" : "RIFF", 4);
	put32(h+4, 4 + (kind == 'l' ? 14 : 0) + 24 + (kind == 'f' ? 12 : 0) + (kind == 'd' ? 0 : 8 + n*Raw));
	memmove(h+8, "WAVE", 4);
	len = 12;
	if (kind == 'l') {
		memmove(h+len, "LIST", 4);
		put32(h+len+4, 5);
		memmove(h+len+8, "INFO\0\0", 6);
		len += 14;
	}
	memmove(h+len, "fmt ", 4);
	put32(h+len+4, 16);
	put16(h+len+8, 1);
	put16(h+len+10, kind == 'm' ? 1 : 2);
	put32(h+len+12, kind == 'r' ? 22050 : 44100);
	put32(h+len+16, 44100*4);
	put16(h+len+20, 4);
	put16(h+len+22, kind == 'b' ? 8 : 16);
	len += 24;
	if (kind == 'f') {
		memmove(h+len, "fact", 4);
		put32(h+len+4, 4);
		put32(h+len+8, n*Samples);
		len += 12;
	}
	if (kind != 'd') {
		memmove(h+len, "data", 4);
		put32(h+len+4, n*Raw);
		len += 8;
	}
	if (fwrite(h, 1, len, w) != len)
		fatal("%s: cannot write", path);
	for (i = 0; kind != 'd' && i < n; i++) {
		if (sectread(d, from+i, 0, f) < 0)
			memset(f, 0, Raw);
		if (fwrite(f, 1, Raw, w) != Raw)
			fatal("%s: cannot write", path);
	}
	if (fclose(w) != 0)
		fatal("%s: cannot write", path);
}

/*
 * The tree of d in root: every file as the disc holds it, then its
 * audio tracks as trackNN.wav, each from its start to the next one's,
 * and for the tracks after them WAV files the port must refuse.
 */
static void
mktree(Disc *d, char *root)
{
	static char kinds[] = "mrbxd";
	uint8_t rec[256], s[Sector];
	char path[Npath], *p;
	uint32_t ext, size, i, k;
	int t;
	FILE *w;

	for (k = 0; k < (uint32_t)d->nname; k++) {
		snprintf(path, sizeof path, "%s%s", root, d->file[k]);
		for (p = path + strlen(root); *p != 0; p++)
			if (*p == '\\') {
				*p = 0;
				if (mkdir(path, 0777) < 0 && access(path, W_OK) < 0)
					fatal("%s: cannot make it", path);
				*p = '/';
			}
		if (walk(d, d->file[k], rec) < 0)
			fatal("%s: lost", d->file[k]);
		ext = get32(rec+2);
		size = get32(rec+10);
		w = fopen(path, "wb");
		if (w == NULL)
			fatal("%s: cannot write", path);
		for (i = 0; i*Sector < size; i++) {
			sectread(d, ext+i, 1, s);
			if (fwrite(s, 1, size - i*Sector < Sector ? size - i*Sector : Sector, w) == 0)
				fatal("%s: cannot write", path);
		}
		if (fclose(w) != 0)
			fatal("%s: cannot write", path);
	}
	for (t = 2; t <= d->ntrack; t++) {
		snprintf(path, sizeof path, "%s/track%02d.wav", root, t);
		putwav(path, d, d->t[t].start, (t < d->ntrack ? d->t[t+1].start : d->leadout) - d->t[t].start,
			t == 3 ? 'l' : t == 4 ? 'f' : 0);
	}
	for (k = 0; kinds[k] != 0; k++) {
		snprintf(path, sizeof path, "%s/track%02d.wav", root, d->ntrack + 1 + k);
		putwav(path, d, d->t[2].start, 10, kinds[k]);
	}
}

/* A directory record at r, its length. */
static int
mkrec(uint8_t *r, char *name, uint32_t ext, uint32_t size, int isdir)
{
	int n, len;

	n = strlen(name);
	if (n == 0)
		n = 1;
	len = 33 + n + (n % 2 == 0);
	memset(r, 0, len);
	r[0] = len;
	put32(r+2, ext);
	r[6] = ext>>24; r[7] = ext>>16; r[8] = ext>>8; r[9] = ext;
	put32(r+10, size);
	r[14] = size>>24; r[15] = size>>16; r[16] = size>>8; r[17] = size;
	r[18] = 94;
	r[19] = 10;
	r[20] = 15;
	r[25] = isdir ? 2 : 0;
	r[28] = 1;
	r[31] = 1;
	r[32] = n;
	if (name[0] != 0)
		memmove(r+33, name, n);
	return len;
}

/*
 * A disc of the oracle's own, as LBA's are laid out: a data track with
 * an ISO 9660, then three audio tracks, written into dir as name.cue and
 * name.bin. lba gives it the copyright file "CD_LBA." and the file
 * CD_LBA in its root.
 */
static void
mkdisc(char *name, int lba)
{
	static struct
	{
		char	*dir;
		char	*name;
		uint32_t	size;
	} files[] = {
		{"VOX", "EN_SYS.VOX;1", 1},
		{"VOX", "EN_CRE.VOX;1", 2047},
		{"VOX", "EN_GAM.VOX;1", 2048},
		{"VOX", "EN_000.VOX;1", 2049},
		{"VOX", "FR_SYS.VOX;1", 4096},
		{"VOX", "FR_000.VOX;1", 0},
		{"VOX", "DE_SYS.VOX;1", 65537},
		{"VOX", "DE_000.VOX;1", 131073},
		{"DATA", "W63488.BIN;1", 63488},
		{"DATA", "W65535.BIN;1", 65535},
		{"DATA", "W129024.BIN;1", 129024},
		{"DATA", "X65536.BIN;1", 65536},
	};
	enum
	{
		Nfiles = sizeof files / sizeof files[0],
		Root = 18,
		Lba = 19,
		Vox = 20,
		Data = 21,
		First = 22,
		Gap = 75,	/* INDEX 00 gaps, held in the file */
		Pregap = 30,	/* the last track's PREGAP, silence outside the file */
	};
	static uint32_t len[] = {300, 200, 150};	/* the audio tracks' sectors */
	uint8_t s[Sector], raw[Raw], *dirs[4];
	uint32_t ext[Nfiles], next, nsect, a, i, j, k, f;
	int off[4];
	char path[Npath], t0[16], t1[16];
	FILE *bin, *cue;

	next = First + lba;
	for (i = 0; i < Nfiles; i++) {
		ext[i] = next;
		next += (files[i].size + Sector - 1) / Sector;
	}
	nsect = next + 2;
	for (i = 0; i < 4; i++) {
		dirs[i] = calloc(1, Sector);
		if (dirs[i] == NULL)
			fatal("out of memory");
		off[i] = 0;
	}
	off[0] += mkrec(dirs[0]+off[0], "", Root, Sector, 1);
	off[0] += mkrec(dirs[0]+off[0], "\1", Root, Sector, 1);
	if (lba)
		off[0] += mkrec(dirs[0]+off[0], "CD_LBA;1", First, 22, 0);
	off[0] += mkrec(dirs[0]+off[0], "DATA", Data, Sector, 1);
	off[0] += mkrec(dirs[0]+off[0], "LBA", Lba, Sector, 1);
	off[1] += mkrec(dirs[1]+off[1], "", Lba, Sector, 1);
	off[1] += mkrec(dirs[1]+off[1], "\1", Root, Sector, 1);
	off[1] += mkrec(dirs[1]+off[1], "VOX", Vox, Sector, 1);
	off[2] += mkrec(dirs[2]+off[2], "", Vox, Sector, 1);
	off[2] += mkrec(dirs[2]+off[2], "\1", Lba, Sector, 1);
	off[3] += mkrec(dirs[3]+off[3], "", Data, Sector, 1);
	off[3] += mkrec(dirs[3]+off[3], "\1", Root, Sector, 1);
	for (i = 0; i < Nfiles; i++) {
		k = strcmp(files[i].dir, "VOX") == 0 ? 2 : 3;
		off[k] += mkrec(dirs[k]+off[k], files[i].name, ext[i], files[i].size, 0);
	}
	snprintf(path, sizeof path, "%s/%s.bin", dir, name);
	bin = fopen(path, "wb");
	if (bin == NULL)
		fatal("%s: cannot write", path);
	for (a = 0; a < nsect; a++) {
		memset(s, 0, Sector);
		if (a == 16) {
			s[0] = 1;
			memmove(s+1, "CD001", 5);
			s[6] = 1;
			memset(s+8, ' ', 32);
			memset(s+40, ' ', 32);
			memmove(s+40, "CDORACLE", 8);
			put32(s+80, nsect);
			s[84] = nsect>>24; s[85] = nsect>>16; s[86] = nsect>>8; s[87] = nsect;
			s[120] = 1; s[123] = 1; s[124] = 1; s[127] = 1;
			put16(s+128, Sector);
			s[130] = Sector>>8;
			mkrec(s+156, "", Root, Sector, 1);
			memset(s+190, ' ', 702-190);
			memset(s+702, ' ', 37*3);
			memmove(s+702, lba ? "CD_LBA." : "OTHER", lba ? 7 : 5);
			s[881] = 1;
		} else if (a == 17) {
			s[0] = 255;
			memmove(s+1, "CD001", 5);
			s[6] = 1;
		} else if (a >= Root && a <= Data)
			memmove(s, dirs[a-Root], Sector);
		else if (lba && a == First)
			memmove(s, "15/08/94 Lba Demo\r\n\032\0\0", 22);
		else
			for (i = 0; i < Nfiles; i++)
				if (a >= ext[i] && (a - ext[i])*Sector < files[i].size)
					for (j = 0; j < Sector && (a - ext[i])*Sector + j < files[i].size; j++)
						s[j] = i*31 + ((a - ext[i])*Sector + j)*7 + (j>>8);
		memset(raw, 0, Raw);
		memset(raw+1, 0xff, 10);
		f = a + Pad;
		raw[12] = f/75/60/10<<4 | f/75/60%10;
		raw[13] = f/75%60/10<<4 | f/75%60%10;
		raw[14] = f%75/10<<4 | f%75%10;
		raw[15] = 1;
		memmove(raw+16, s, Sector);
		if (fwrite(raw, 1, Raw, bin) != Raw)
			fatal("%s: cannot write", path);
	}
	for (k = 0; k < 3; k++)
		for (a = 0; a < (k > 0 ? Gap : 0) + len[k]; a++) {
			for (j = 0; j < Raw; j++)
				raw[j] = k > 0 && a < Gap ? (0x80 + k*5 + a*7 + j) & 0xff : (k*13 + a*3 + j) & 0xff;
			if (fwrite(raw, 1, Raw, bin) != Raw)
				fatal("%s: cannot write", path);
		}
	if (fclose(bin) != 0)
		fatal("%s: cannot write", path);
	snprintf(path, sizeof path, "%s/%s.cue", dir, name);
	cue = fopen(path, "w");
	if (cue == NULL)
		fatal("%s: cannot write", path);
	fprintf(cue, "FILE \"%s.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n", name);
	fprintf(cue, "  TRACK 02 AUDIO\n    PREGAP 00:02:00\n    INDEX 01 %s\n", cuetime(nsect, t1));
	a = nsect + len[0];
	for (k = 1; k < 3; k++) {
		fprintf(cue, "  TRACK %02u AUDIO\n", k+2);
		if (k == 2)
			fprintf(cue, "    PREGAP %s\n", cuetime(Pregap, t0));
		fprintf(cue, "    INDEX 00 %s\n    INDEX 01 %s\n", cuetime(a, t0), cuetime(a + Gap, t1));
		a += Gap + len[k];
	}
	if (fclose(cue) != 0)
		fatal("%s: cannot write", path);
	for (i = 0; i < 4; i++)
		free(dirs[i]);
}

/*
 * d, a disc of one file, written again into dir as namesplit.cue over
 * a file for each track, namesplitN.bin, the same disc to a drive: each
 * audio track's file from its INDEX 00, but track 3's, which opens on
 * the last Tail frames of track 2, each PREGAP kept.
 */
static void
mksplit(Disc *d, char *name)
{
	enum
	{
		Tail = 10,
	};
	uint8_t raw[Raw];
	uint32_t first, last, a;
	char path[Npath], cuepath[Npath], t0[16];
	FILE *bin, *cue;
	int k;

	snprintf(cuepath, sizeof cuepath, "%s/%ssplit.cue", dir, name);
	cue = fopen(cuepath, "w");
	if (cue == NULL)
		fatal("%s: cannot write", cuepath);
	for (k = 1; k <= d->ntrack; k++) {
		first = k == 1 ? 0 : d->t[k].idx0 - (k == 3 ? Tail : 0);
		last = k == d->ntrack ? d->nsect[0] : d->t[k+1].idx0 - (k+1 == 3 ? Tail : 0);
		snprintf(path, sizeof path, "%s/%ssplit%d.bin", dir, name, k);
		bin = fopen(path, "wb");
		if (bin == NULL || fseek(d->f[0], (long)first*Raw, SEEK_SET) != 0)
			fatal("%s: cannot write", path);
		for (a = first; a < last; a++)
			if (fread(raw, 1, Raw, d->f[0]) != Raw || fwrite(raw, 1, Raw, bin) != Raw)
				fatal("%s: cannot write", path);
		if (fclose(bin) != 0)
			fatal("%s: cannot write", path);
		fprintf(cue, "FILE \"%ssplit%d.bin\" BINARY\n  TRACK %02d %s\n", name, k, k, k == 1 ? "MODE1/2352" : "AUDIO");
		if (d->t[k].gap != 0)
			fprintf(cue, "    PREGAP %s\n", cuetime(d->t[k].gap, t0));
		if (d->t[k].idx0 != d->t[k].idx1)
			fprintf(cue, "    INDEX 00 %s\n", cuetime(d->t[k].idx0 - first, t0));
		fprintf(cue, "    INDEX 01 %s\n", cuetime(d->t[k].idx1 - first, t0));
	}
	if (fclose(cue) != 0)
		fatal("%s: cannot write", cuepath);
}

static void
usage(void)
{
	fprintf(stderr, "usage: cdoracle -d dir lba0.elf lba0.sym [image tree tracks]...\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	static char *faults[] = {
		"no disc", "open tray", "tray stays open", "second drive", "missing file", "audio info",
		"second track", "retries", "missing track", "status after reset", "status failing",
		"caller's CX", "convention", "DOS memory", "drive letters", "DPMI failing", "drive A",
	};
	char path[Npath], label[Npath], tree[Npath], **disc;
	Disc *lba, *other, *split;
	Run r;
	Fault f;
	int i, nbad, ndisc;

	oracle = "cdoracle";
	dir = NULL;
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (strcmp(argv[i], "-d") == 0 && i+1 < argc)
			dir = argv[++i];
		else
			usage();
	}
	if (dir == NULL || argc - i < 2 || (argc - i - 2) % 3 != 0)
		usage();
	disc = argv + i + 2;
	ndisc = (argc - i - 2) / 3;
	pcload(argv[i], argv[i+1]);
	pcint(0x2f, mscdex);
	lowbuf = lowalloc(32*Sector);
	as.initcdr = lookup("InitCDR");
	as.clearcdr = lookup("ClearCDR");
	as.stopcdr = lookup("StopCDR");
	as.playtrackcdr = lookup("PlayTrackCDR");
	as.getlengthtrackcdr = lookup("GetLengthTrackCDR");
	as.getfilecdr = lookup("GetFileCDR");
	as.readlongcdr = lookup("ReadLongCDR");
	as.getcopyrightname = lookup("GetCopyrightName");
	as.lockcdr = lookup("LockCDR");
	as.drivecdr = (int16_t*)lookup("DriveCDR");
	as.start = (int32_t*)lookup("FileCD_Start");
	as.sect = (int32_t*)lookup("FileCD_Sect");
	as.size = (int32_t*)lookup("FileCD_Size");
	as.lowtrack = (uint8_t*)lookup("LowTrack");
	as.higtrack = (uint8_t*)lookup("HigTrack");
	as.leadout = (uint32_t*)lookup("LeadOut");
	if (mkdir(dir, 0777) < 0 && access(dir, W_OK) < 0)
		fatal("%s: cannot make it", dir);
	snprintf(prefdir, sizeof prefdir, "%s/pref", dir);
	if (mkdir(prefdir, 0777) < 0 && access(prefdir, W_OK) < 0)
		fatal("%s: cannot make it", prefdir);
	mkdisc("lba", 1);
	mkdisc("other", 0);
	snprintf(path, sizeof path, "%s/lba.cue", dir);
	lba = discopen(path);
	snprintf(path, sizeof path, "%s/other.cue", dir);
	other = discopen(path);
	snprintf(tree, sizeof tree, "%s/lbatree", dir);
	if (mkdir(tree, 0777) < 0 && access(tree, W_OK) < 0)
		fatal("%s: cannot make it", tree);
	mktree(lba, tree);
	mksplit(lba, "lba");
	snprintf(path, sizeof path, "%s/lbasplit.cue", dir);
	split = discopen(path);
	nbad = 0;
	memset(&r, 0, sizeof r);
	r.label = "lba.cue";
	r.disc = lba;
	snprintf(path, sizeof path, "%s/lba.cue", dir);
	r.image = strdup(path);
	r.whole = 1;
	nbad += inchild(r.label, 600, comparerun, &r);
	r.label = "lba.cue, its tree";
	r.image = NULL;
	r.tree = tree;
	nbad += inchild(r.label, 600, comparerun, &r);
	r.tree = NULL;
	r.label = "lbasplit.cue, the port's";
	snprintf(path, sizeof path, "%s/lbasplit.cue", dir);
	r.image = strdup(path);
	nbad += inchild(r.label, 600, comparerun, &r);
	r.label = "lbasplit.cue, the drive's";
	r.disc = split;
	snprintf(path, sizeof path, "%s/lba.cue", dir);
	r.image = strdup(path);
	nbad += inchild(r.label, 600, comparerun, &r);
	r.label = "other.cue";
	r.disc = other;
	snprintf(path, sizeof path, "%s/other.cue", dir);
	r.image = strdup(path);
	r.whole = 0;
	nbad += inchild(r.label, 600, comparerun, &r);
	r.label = "no drive";
	r.disc = NULL;
	r.image = NULL;
	nbad += inchild(r.label, 600, comparerun, &r);
	for (i = 0; i < (int)(sizeof faults / sizeof faults[0]); i++) {
		f.name = faults[i];
		f.lba = lba;
		f.other = other;
		nbad += inchild(faults[i], 10, faultrun, &f);
	}
	for (i = 0; i < ndisc; i++) {
		memset(&r, 0, sizeof r);
		r.disc = discopen(disc[3*i]);
		r.whole = 1;
		r.label = r.disc->name;
		r.image = disc[3*i];
		nbad += inchild(r.label, 600, comparerun, &r);
		if (strcmp(disc[3*i+1], "-") == 0)
			continue;
		snprintf(label, sizeof label, "%s, its tree", r.disc->name);
		r.label = label;
		r.image = NULL;
		r.tree = disc[3*i+1];
		r.tracks = strcmp(disc[3*i+2], "-") != 0 ? disc[3*i+2] : NULL;
		nbad += inchild(r.label, 600, comparerun, &r);
	}
	if (nbad > 0) {
		fprintf(stderr, "cdoracle: %d failed\n", nbad);
		return 1;
	}
	printf("cdoracle: ok\n");
	return 0;
}
