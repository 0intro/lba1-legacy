#include <dirent.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/*
 * lbasave: a save of Little Big Adventure 1 written as another version
 * of the game writes it.
 *
 *	lbasave -t version [-n name] [-T seconds] [-g gamedir] file... dir
 *	lbasave -t version [-n name] [-T seconds] [-g gamedir] file out.LBA
 *	lbasave -l [-g gamedir] file...
 *
 * Every version keeps a game in the 1994 record: its version byte, the
 * player's name, the game's flags, the scene and its entry, the hero,
 * the holomap, the inventory. Every version reads the record whatever
 * its version byte, and writes it back when it loads it. The versions:
 *
 *	lba	LBA.EXE and RELENT.EXE: the record, version 3, as S<n>.LBA
 *		in the game's directory, which lists 50, a name of 29
 *		characters at most
 *	lbawin	LbaWin 0.8.1: the same, in Save in its directory, a name of
 *		28 at most, as its list loses a save of 29
 *	classic	2.21's Classic, GOG's and Steam's: the record, version 3,
 *		New Game+ marked in bit 7 of the flags, as S<n>.LBA in
 *		Saved Games\2point21\tlba-classic\Save
 *	dotemu	DotEmu's, Android 1.06 and the Enhanced Edition: the record,
 *		version 4, then the play time, 8 bytes of seconds, in a slot
 *		named by its date, YYYYMMDDhhmmss.LBA, with its picture
 *		beside it as a PNG, in Android/data/com.dotemu.lba/data or
 *		DotEmu\saves, a name of 28 at most
 *	legacy	LBA1 Legacy: the record, version 3, or version 16 with the
 *		scene as it ran after it (the tail: a picture of 160x120,
 *		the context, their CRC-32), packed in a player's save
 *		S<n>.LBA, as it is in autosave.lba and current.lba, in the
 *		preferences
 *
 * -t writes each file as the version writes a save, into dir under the
 * name the version gives a new one (the file's own number or date when
 * free), or into out.LBA, and prints each file it writes. It keeps what
 * the version keeps and says on standard error what it drops: New
 * Game+ for any version but the Classic, the play time for any version
 * but DotEmu's, the scene as it ran for any version but LBA1 Legacy,
 * which loads the save at its scene's entry. -n names the player in
 * ASCII, whatever the shell's encoding makes of the font's other letters,
 * as the game's copy of a save does. A name longer than the version's list
 * reads is refused, and so, into a directory, is a name another save
 * there has, which the PC's versions refuse when it is typed (DotEmu's
 * list shows the scene and the time alone). -T gives DotEmu's play
 * time, otherwise the file's own, or 0. -g names the game's directory,
 * whose RESS.HQR gives the palette of DotEmu's picture made from
 * LBA1 Legacy's, and whose ANIM.HQR and SCENE.HQR the upgrade of a
 * snapshot reads.
 *
 * LBA1 Legacy's earlier saves, which it loads as their record, become
 * its version 16 with -t legacy: those of version 16 written before the
 * tail ended with its CRC-32, those of version 17, LBA2's first form,
 * and the snapshots, version 16 with "SNAP" after the record.
 *
 * -l prints each save, a field and its value a line, tab-separated, a
 * blank line between saves.
 *
 * The status is 1 when a file could not be read as a save or written,
 * 2 for a usage error.
 */

enum
{
	Nname = 29,	/* a name's characters, MAX_SIZE_PLAYER_NAME */
	Nflags = 255,	/* MAX_FLAGS_GAME */
	Nholo = 150,	/* MAX_HOLO_POS */
	Ninv = 28,	/* MAX_INVENTORY */
	Mark = 0x80,	/* New Game+, in a flag */
	Packed = 0x80,	/* SAVE_COMPRESS, in the version byte: the tail packed */
	Vpc = 3,
	Vdotemu = 4,
	Vport = 16,
	Vlba2 = 17,	/* LBA1 Legacy's first LBA2 form */
	Maxtime = 356400,	/* DotEmu's play time, 99 hours */
	Ntime = 8,	/* the play time's bytes */
	Picx = 160,	/* LBA1 Legacy's picture */
	Picy = 120,
	Npic = Picx*Picy,
	Ntail = 1<<20,	/* SIZE_SAVE: the room for a tail */
	Nfile = 2<<20,	/* the largest save read */
	Ncube = 64*25*64*2,	/* the grid's bytes, SIZE_CUBE */
	Nobj = 100,	/* MAX_OBJETS */
	Nzones = 255,	/* MAX_ZONES */
	Nextras = 50,	/* MAX_EXTRAS */
	Extra = 34,	/* T_EXTRA's bytes */
	Nincrust = 10,	/* MAX_INCRUST_DISP */
	Incrust = 16,	/* T_INCRUST_DISP's bytes */
	Globals = 156,	/* today's context before the actors' count */
	Actor = 147,	/* an actor in today's context */
	Own = 792,	/* LBA1's words and palette in the context */
	Block = 105,	/* an actor's fields in the earlier forms */
	Vars = 2798,	/* a snapshot's globals */
	Animbuf = 5000,	/* BufferAnim */
	Sprite3d = 1024,	/* SPRITE_3D, in an actor's Flags */
	Keynone = -1,	/* KEY_NONE: the body comes from no frame */
	Keyblend = -2,	/* KEY_BLEND: it comes from a blend */
	Nsaves = 10000,	/* S<n>.LBA, as Rnd(10000) numbers them */
	Npath = 4096,
};

typedef struct Buf Buf;
typedef struct Part Part;
typedef struct Save Save;
typedef struct Version Version;
typedef struct Walk Walk;

/* bytes written in turn, as SaveContexte writes them */
struct Buf
{
	uint8_t	*p;
	long	n;
	long	max;
	int	cut;	/* a value its width cuts */
};

/* a part of the record after the name: a count and its table, or bytes */
struct Part
{
	char	*what;
	int	count;
	int	n;
};

/* a save read */
struct Save
{
	char	*path;
	uint8_t	*b;
	long	n;
	int	version;	/* the version byte, Packed cleared */
	long	name;	/* the name's NUL */
	long	rec;	/* the record's end */
	int	hastime;
	long long	time;	/* DotEmu's play time */
	char	*form;	/* the tail's form: today's, or an earlier one */
	uint8_t	*tail;	/* the tail in today's form */
	long	ntail;
	int	pic;	/* the tail's picture is the scene's */
	uint8_t	*snap;	/* the snapshot, upgraded when -t legacy asks */
	long	nsnap;
	char	*why;	/* why the tail was dropped */
	long	extra;	/* bytes after the record dropped */
	int	marks;	/* New Game+ */
};

/* a version of the game: what it writes */
struct Version
{
	char	*name;
	int	version;	/* the version byte */
	int	maxname;	/* the longest name its list reads */
	int	marks;	/* keeps New Game+ */
	int	time;	/* writes the play time */
	int	tail;	/* writes LBA1 Legacy's tail */
	int	dated;	/* names a save by its date */
	int	names;	/* refuses a name another save has */
};

/* what a walk of today's context found */
struct Walk
{
	long	nobj;
	uint32_t	timer;
	uint32_t	checksum;
	long	hero[3];
	long	npatch;
	long	nzones;
	long	nincrust;
	long	runs;
	long	loops;
};

/* sources/patch.c's places of each scene, its T_PATCH as defines.h declares it */
typedef struct
{
	int16_t	Size;
	int16_t	Offset;
} T_PATCH;

extern T_PATCH	TabPatches[];
extern uint16_t	IndexPatches[];
extern int32_t	NbIndexPatches;

unsigned long	Compress_LZSS(char*, char*, unsigned long);

static Part parts[] = {
	{"the flags", Nflags, 0},
	{"the scene and the hero", 0, 18},
	{"the holomap's places", Nholo, 0},
	{"the fuel", 0, 1},
	{"the inventory", Ninv, 0},
	{"the clovers and the weapon", 0, 3},
};

static Version versions[] = {
	{"lba", Vpc, 29, 0, 0, 0, 0, 1},
	{"lbawin", Vpc, 28, 0, 0, 0, 0, 1},
	{"classic", Vpc, 29, 1, 0, 0, 0, 1},
	{"dotemu", Vdotemu, 28, 0, 1, 0, 1, 0},
	{"legacy", Vpc, 29, 0, 0, 1, 0, 1},
};

static char *gamedir;
static int status;
static char whybuf[128];

static void
usage(void)
{
	fprintf(stderr, "usage: lbasave -t version [-n name] [-T seconds] [-g gamedir] file... dir|out.LBA\n");
	fprintf(stderr, "       lbasave -l [-g gamedir] file...\n");
	exit(2);
}

static void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "lbasave: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

/* A file that failed, said and counted in the status. */
static void
complain(char *path, char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "lbasave: %s: ", path);
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	status = 1;
}

/* What a conversion drops, said. */
static void
note(char *path, char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "lbasave: %s: ", path);
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
}

/* fmt's text into buf of n bytes: 0, or -1 when it is cut. */
static int
format(char *buf, size_t n, char *fmt, ...)
{
	va_list arg;
	int r;

	va_start(arg, fmt);
	r = vsnprintf(buf, n, fmt, arg);
	va_end(arg);
	return r < 0 || (size_t)r >= n ? -1 : 0;
}

static void*
emalloc(size_t n)
{
	void *p;

	p = malloc(n);
	if (p == NULL)
		fatal("out of memory");
	return p;
}

static uint32_t
get16(uint8_t *p)
{
	return p[0] | p[1]<<8;
}

static uint32_t
get32(uint8_t *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24;
}

static long
s16(uint8_t *p)
{
	return (int16_t)get16(p);
}

static long
s32(uint8_t *p)
{
	return (int32_t)get32(p);
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}

static void
putbe32(uint8_t *p, uint32_t v)
{
	p[0] = v>>24;
	p[1] = v>>16;
	p[2] = v>>8;
	p[3] = v;
}

/* The CRC-32 of n bytes, zlib's, as LBA1 Legacy's saves end with it. */
static uint32_t
crc32(uint32_t crc, uint8_t *p, long n)
{
	int k;

	crc = ~crc;
	while (n-- > 0) {
		crc ^= *p++;
		for (k = 0; k < 8; k++)
			crc = (crc>>1) ^ (0xEDB88320 & -(crc&1));
	}
	return ~crc;
}

/*
 * The LZS stream of n bytes at src expanded into size bytes at dst, as
 * Expand reads it, each step checked: 0, or -1 for a stream that runs
 * out or copies from before dst, or, when exact, holds bytes past its
 * last.
 */
static int
expand(uint8_t *src, long n, uint8_t *dst, long size, int exact)
{
	uint8_t *s, *es, *d, *ed;
	uint32_t flags, w, len, back;
	int i;

	s = src;
	es = src + n;
	d = dst;
	ed = dst + size;
	while (d < ed) {
		if (s >= es)
			return -1;
		flags = *s++;
		for (i = 0; i < 8 && d < ed; i++, flags >>= 1) {
			if (flags & 1) {
				if (s >= es)
					return -1;
				*d++ = *s++;
				continue;
			}
			if (es - s < 2)
				return -1;
			w = get16(s);
			s += 2;
			len = (w&0xF) + 2;
			back = (w>>4) + 1;
			if (back > (uint32_t)(d - dst) || len > (uint32_t)(ed - d))
				return -1;
			for (; len > 0; len--, d++)
				*d = *(d-back);
		}
	}
	if (exact && s != es)
		return -1;
	return 0;
}

static void
bput(Buf *b, void *v, long n)
{
	if (n == 0)
		return;
	if (b->n + n > b->max) {
		while (b->n + n > b->max)
			b->max = b->max*2 + 1024;
		b->p = realloc(b->p, b->max);
		if (b->p == NULL)
			fatal("out of memory");
	}
	memcpy(b->p + b->n, v, n);
	b->n += n;
}

/* A byte, as LbaPutByte writes it, refusing what it cuts. */
static void
bbyte(Buf *b, long v)
{
	uint8_t c;

	if (v < 0 || v > 255)
		b->cut = 1;
	c = v;
	bput(b, &c, 1);
}

/* A word, as LbaPutWord writes it. */
static void
bword(Buf *b, long v)
{
	uint8_t w[2];

	if (v < -32768 || v > 65535)
		b->cut = 1;
	w[0] = v;
	w[1] = (uint32_t)v>>8;
	bput(b, w, 2);
}

static void
blong(Buf *b, uint32_t v)
{
	uint8_t l[4];

	put32(l, v);
	bput(b, l, 4);
}

/* The places scene's scripts rewrite, their count and their bytes. */
static long
places(int scene, T_PATCH **p, long *size)
{
	long n, i;

	*p = TabPatches;
	*size = 0;
	if (scene < 0 || scene >= NbIndexPatches)
		return 0;
	*p = &TabPatches[IndexPatches[scene]];
	n = IndexPatches[scene+1] - IndexPatches[scene];
	for (i = 0; i < n; i++)
		*size += (*p)[i].Size;
	return n;
}

/*
 * Today's context, n bytes at c, walked as LoadContexte reads it
 * through, its counts checked against the tables and scene's places:
 * 0, or -1 with why.
 */
static int
walk(uint8_t *c, long n, int scene, Walk *w, char **why)
{
	T_PATCH *pl;
	long p, i, key, size, off, len;

	if (n < Globals + 4) {
		*why = "cut short in its globals";
		return -1;
	}
	w->timer = get32(c+93);
	w->checksum = get32(c+101);
	w->nobj = s32(c+156);
	if (w->nobj < 1 || w->nobj > Nobj) {
		*why = "its actors' count is out of its table";
		return -1;
	}
	p = Globals + 4;
	if (n - p < Actor * w->nobj) {
		*why = "cut short in its actors";
		return -1;
	}
	for (i = 0; i < 3; i++)
		w->hero[i] = s32(c + p + 98 + 4*i);
	for (i = 0; i < w->nobj; i++, p += Actor) {
		key = s32(c + p + 119);
		if (c[p+114] > 1 || key < Keyblend) {
			*why = "an actor's body is out of its range";
			return -1;
		}
	}
	if (n - p < 8) {
		*why = "cut short in its patches";
		return -1;
	}
	w->npatch = s32(c+p);
	if (w->npatch != places(scene, &pl, &size) || (long)get32(c+p+4) != size) {
		*why = "its patches are another scene's";
		return -1;
	}
	p += 8;
	if (n - p < size + 1 + Nextras*Extra + 4) {
		*why = "cut short in its patches or extras";
		return -1;
	}
	p += size;
	if (c[p] != Nextras) {
		*why = "its extras' count is out of its table";
		return -1;
	}
	p += 1 + Nextras*Extra;
	w->nzones = s32(c+p);
	if (w->nzones < 0 || w->nzones > Nzones) {
		*why = "its zones' count is out of its table";
		return -1;
	}
	p += 4;
	if (n - p < 12*w->nzones + 1) {
		*why = "cut short in its zones";
		return -1;
	}
	p += 12*w->nzones;
	w->nincrust = c[p];
	if (w->nincrust > Nincrust) {
		*why = "its overlays' count is out of its table";
		return -1;
	}
	p += 1;
	if (n - p < Incrust*w->nincrust + 12 + Own) {
		*why = "cut short in its overlays or its own";
		return -1;
	}
	p += Incrust*w->nincrust + 12 + Own;
	for (w->runs = 0;; w->runs++) {
		if (n - p < 4) {
			*why = "cut short in its grid";
			return -1;
		}
		off = get32(c+p);
		p += 4;
		if (off == Ncube)
			break;
		if (n - p < 4) {
			*why = "cut short in its grid";
			return -1;
		}
		len = get32(c+p);
		p += 4;
		if (off < 0 || off > Ncube || len < 0 || len > Ncube - off || n - p < len) {
			*why = "its grid's runs are out of the grid";
			return -1;
		}
		p += len;
	}
	if (n - p < 1) {
		*why = "cut short in its samples";
		return -1;
	}
	w->loops = c[p];
	p += 1;
	if (n - p != 12*w->loops + 4) {
		*why = n - p < 12*w->loops + 4 ? "cut short in its samples" : "it holds bytes past its end";
		return -1;
	}
	return 0;
}

/* An actor of the earlier forms' fields, a, written as today's. */
static void
actor(Buf *o, uint8_t *a, int has, long key, uint32_t keytime)
{
	int i;

	bbyte(o, a[0]);	/* GenBody */
	bbyte(o, a[3]);	/* Col */
	bword(o, a[1]);	/* GenAnim */
	bword(o, a[2]);	/* NextGenAnim */
	for (i = 0; i < 3; i++)
		blong(o, s16(a + 22 + 2*i));	/* OldPosX, Y, Z */
	for (i = 0; i < 4; i++)
		blong(o, s16(a + 60 + 2*i));	/* Info to Info3 */
	bword(o, s16(a+82));	/* HitBy */
	bbyte(o, s16(a+84));	/* HitForce */
	bword(o, s16(a+86));	/* LifePoint */
	bword(o, s16(a+8));	/* OptionFlags */
	bword(o, s16(a+4));	/* Sprite */
	bword(o, s16(a+6));	/* OffsetLabelTrack */
	bbyte(o, s16(a+12));	/* Armure */
	for (i = 0; i < 6; i++)
		bword(o, s16(a + 28 + 2*i));	/* Xmin to Zmax */
	bput(o, a+44, 10);	/* RealAngle */
	bword(o, s16(a+56));	/* OffsetTrack */
	bword(o, s16(a+42));	/* SRot */
	bword(o, s16(a+58));	/* OffsetLife */
	bword(o, s16(a+70));	/* CarryBy */
	bbyte(o, s16(a+54));	/* Move */
	bword(o, s16(a+68));	/* ObjCol */
	bword(o, s16(a+72));	/* ZoneSce */
	bword(o, s16(a+74));	/* LabelTrack */
	bword(o, s16(a+76));	/* MemoLabelTrack */
	blong(o, s16(a+78));	/* Flags */
	blong(o, s16(a+80));	/* WorkFlags */
	bword(o, s16(a+96));	/* DoorWidth */
	bword(o, s16(a+102));	/* FlagAnim */
	bbyte(o, a[104]);	/* CodeJeu */
	for (i = 0; i < 3; i++)
		blong(o, s16(a + 16 + 2*i));	/* PosObjX, Y, Z */
	blong(o, s16(a+40));	/* Beta */
	bbyte(o, has);
	blong(o, s16(a+98));	/* Anim */
	blong(o, key);
	blong(o, keytime);
	blong(o, s16(a+100));	/* Frame */
	for (i = 0; i < 3; i++)
		blong(o, s16(a + 90 + 2*i));	/* AnimStepX, Y, Z */
	blong(o, s16(a+88));	/* AnimStepBeta */
}

/*
 * The grid's runs at c+*p, n bytes in all, copied as they are: 0, or -1
 * for runs that go past the grid or the bytes.
 */
static int
runs(Buf *o, uint8_t *c, long n, long *p)
{
	long off, len;

	for (;;) {
		if (n - *p < 4)
			return -1;
		off = get32(c + *p);
		bput(o, c + *p, 4);
		*p += 4;
		if (off == Ncube)
			return 0;
		if (n - *p < 4)
			return -1;
		len = get32(c + *p);
		if (off < 0 || off > Ncube || len < 0 || len > Ncube - off || n - *p - 4 < len)
			return -1;
		bput(o, c + *p, 4 + len);
		*p += 4 + len;
	}
}

/* The samples that loop for ever at c+*p, copied as they are: 0, or -1 for a list cut short. */
static int
loops(Buf *o, uint8_t *c, long n, long *p)
{
	long k;

	if (n - *p < 1)
		return -1;
	k = c[*p];
	if (n - *p < 1 + 12*k)
		return -1;
	bput(o, c + *p, 1 + 12*k);
	*p += 1 + 12*k;
	return 0;
}

/*
 * The version 17's tail, t, size bytes, written in today's form into o,
 * the picture first: 0, or -1 with why. Its context is SaveContexte's
 * of LBA2's first form, each variable at its size in memory.
 */
static int
from17(uint8_t *t, long size, int scene, Buf *o, char **why)
{
	T_PATCH *pl;
	uint8_t *c, *a;
	long n, p, i, nobj, has, key, np, psize, want, nz, ni;
	uint32_t keytime;

	bput(o, t, Npic);
	c = t + Npic;
	n = size - Npic;
	*why = "its context is cut short";
	if (n < 166)
		return -1;
	bput(o, c, 80);	/* ListFlagCube */
	bbyte(o, s16(c+80));	/* NbLittleKeys */
	for (i = 0; i < 3; i++)
		blong(o, get32(c + 82 + 4*i));	/* StartXCube, Y, Z */
	blong(o, get32(c+106));	/* the timer */
	bbyte(o, s16(c+110));	/* NumObjFollow */
	bbyte(o, s16(c+112));	/* SaveComportement */
	bword(o, s16(c+114));	/* SaveBeta */
	blong(o, get32(c+120));	/* the scene's CRC-32 */
	for (i = 0; i < 3; i++)
		blong(o, s16(c + 124 + 2*i));	/* LastMyFire, LastMyJoy, LastJoyFlag */
	bbyte(o, s16(c+130));	/* Bulle */
	bbyte(o, s16(c+132));	/* ActionNormal */
	blong(o, -1);	/* InventoryAction, as the main loop leaves it */
	blong(o, s16(c+134));	/* MagicBall */
	bbyte(o, s16(c+136));	/* MagicBallType */
	bword(o, s16(c+138));	/* MagicBallCount */
	bbyte(o, s16(c+140));	/* FlagClimbing */
	blong(o, s16(c+142));	/* StartYFalling */
	bput(o, c+144, 10);	/* RealFalling */
	blong(o, s16(c+154));	/* StepFalling */
	bbyte(o, s16(c+156));	/* CameraZone */
	blong(o, s16(c+158));	/* InvSelect */
	bword(o, s16(c+160));	/* NumPingouin */
	nobj = s32(c+162);
	if (nobj < 1 || nobj > Nobj) {
		*why = "its actors' count is out of its table";
		return -1;
	}
	blong(o, nobj);
	p = 166;
	for (i = 0; i < nobj; i++) {
		if (n - p < Block + 1)
			return -1;
		a = c + p;
		has = a[Block];
		p += Block + 1;
		key = Keynone;
		keytime = 0;
		if (has > 1) {
			*why = "an actor's body is out of its range";
			return -1;
		}
		if (has && (s16(a+78) & Sprite3d) == 0) {
			if (n - p < 6)
				return -1;
			key = s16(c+p);
			keytime = get32(c+p+2);
			p += 6;
			if (key < Keyblend) {
				*why = "an actor's body is out of its range";
				return -1;
			}
		}
		actor(o, a, has, key, keytime);
	}
	if (n - p < 8)
		return -1;
	np = s32(c+p);
	psize = get32(c+p+4);
	if (np != places(scene, &pl, &want) || psize != want) {
		*why = "its patches are another scene's";
		return -1;
	}
	if (n - p - 8 < psize + Nextras*Extra + 2)
		return -1;
	bput(o, c+p, 8 + psize);
	p += 8 + psize;
	bbyte(o, Nextras);
	bput(o, c+p, Nextras*Extra);
	p += Nextras*Extra;
	nz = s16(c+p);
	p += 2;
	if (nz < 0 || nz > Nzones) {
		*why = "its zones' count is out of its table";
		return -1;
	}
	if (n - p < 8*nz + 1)
		return -1;
	blong(o, nz);
	for (i = 0; i < nz; i++, p += 8) {
		blong(o, s16(c + p + 2));	/* Info1 */
		blong(o, s16(c + p + 4));	/* Info2 */
		blong(o, s16(c + p + 6));	/* Info3 */
	}
	ni = c[p];
	if (ni > Nincrust) {
		*why = "its overlays' count is out of its table";
		return -1;
	}
	if (n - p < 1 + Incrust*ni + Own)
		return -1;
	bput(o, c+p, 1 + Incrust*ni);
	p += 1 + Incrust*ni;
	for (i = 0; i < 3; i++)
		blong(o, get32(c + 94 + 4*i));	/* WorldXCube, Y, Z */
	bput(o, c+p, Own);
	p += Own;
	if (runs(o, c, n, &p) < 0 || loops(o, c, n, &p) < 0)
		return -1;
	if (n - p != 4) {
		*why = n - p < 4 ? "its context is cut short" : "it holds bytes past its end";
		return -1;
	}
	bput(o, c+p, 4);	/* the seed */
	return 0;
}

/* Entry k of the game's HQR file name, expanded, and its size. NULL if it cannot be read. */
static uint8_t*
hqr(char *name, long k, long *size)
{
	char path[Npath];
	struct dirent *e;
	DIR *d;
	FILE *f;
	uint8_t h[10], *packed, *p;
	long off, n, i;
	int method;

	if (gamedir == NULL || k < 0)
		return NULL;
	d = opendir(gamedir);
	if (d == NULL)
		return NULL;
	path[0] = 0;
	while ((e = readdir(d)) != NULL) {
		for (i = 0; name[i] != 0 && e->d_name[i] != 0; i++)
			if ((e->d_name[i] | 0x20) != (name[i] | 0x20))
				break;
		if (name[i] == 0 && e->d_name[i] == 0) {
			if (format(path, sizeof path, "%s/%s", gamedir, e->d_name) < 0)
				path[0] = 0;
			break;
		}
	}
	closedir(d);
	if (path[0] == 0 || (f = fopen(path, "rb")) == NULL)
		return NULL;
	p = NULL;
	if (fread(h, 1, 4, f) == 4 && k < (long)get32(h)/4
	&& fseek(f, 4*k, SEEK_SET) == 0 && fread(h, 1, 4, f) == 4) {
		off = get32(h);
		if (off != 0 && fseek(f, off, SEEK_SET) == 0 && fread(h, 1, 10, f) == 10) {
			*size = get32(h);
			n = get32(h+4);
			method = get16(h+8);
			if (*size > 0 && *size < Nfile && n > 0 && n < Nfile && method <= 1) {
				packed = emalloc(n);
				p = emalloc(*size);
				if ((long)fread(packed, 1, n, f) != n
				|| (method == 0 && n < *size)
				|| (method == 1 && expand(packed, n, p, *size, 0) < 0)) {
					free(p);
					p = NULL;
				} else if (method == 0) {
					memcpy(p, packed, *size);
				}
				free(packed);
			}
		}
	}
	fclose(f);
	return p;
}

/*
 * The keyframe of animation anim at offset off from its start, as
 * KeyFrame finds it, Keyblend for a frame of no keyframe. -3 if the
 * animation cannot be read.
 */
static long
keyframe(long anim, uint32_t off)
{
	uint8_t *a;
	long size, step, k;

	if (anim < 0)
		return Keyblend;
	a = hqr("ANIM.HQR", anim, &size);
	if (a == NULL)
		return -3;
	k = Keyblend;
	if (size >= 8) {
		step = 8*get16(a+2) + 8;
		if (off >= 8 && off - 8 < (uint32_t)size - 8 && (off - 8) % step == 0
		&& (long)((off - 8) / step) < (long)get16(a))
			k = (off - 8) / step;
	}
	free(a);
	return k;
}

/*
 * The zones of a scene's data, n bytes at s, found as LoadScene walks
 * it: their count, and their first's offset in *z. -1 if the walk runs
 * past the data.
 */
static long
zones(uint8_t *s, long n, long *z)
{
	long p, i, k, nobj;

	p = 45;
	for (i = 0; i < 2; i++) {
		if (n - p < 2)
			return -1;
		p += 2 + get16(s+p);
	}
	if (n - p < 2)
		return -1;
	nobj = get16(s+p);
	p += 2;
	for (k = 1; k < nobj; k++) {
		p += 35;
		for (i = 0; i < 2; i++) {
			if (n - p < 2)
				return -1;
			p += 2 + get16(s+p);
		}
	}
	if (n - p < 2)
		return -1;
	*z = p + 2;
	if (n - *z < 24*(long)get16(s+p))
		return -1;
	return get16(s+p);
}

/*
 * A snapshot, b, n bytes, written in today's form into o: 0, or -1
 * with why. It holds the scene's data whole, each variable at its size
 * in memory, each body with the place of its frame. The keyframes come
 * from ANIM.HQR, and the scene's data, of which today's keeps the
 * places its scripts rewrite and the zones' Info1 to Info3, must be
 * SCENE.HQR's elsewhere.
 */
static int
fromsnap(uint8_t *b, long n, int scene, Buf *o, char **why)
{
	T_PATCH *pl;
	uint8_t *s, *v, *a, *hq, *keep, black[Npic];
	long size, p, i, k, j, nobj, np, psize, nz, z, kind, has, key, g, ni, hqsize;
	uint32_t where, off, keytime;

	*why = "its snapshot is cut short";
	if (n < 22 + 4)
		return -1;
	if (get16(b) != 1 || (long)get16(b+2) != scene) {
		*why = "its snapshot is another layout's or another scene's";
		return -1;
	}
	size = get32(b+4);
	nobj = get16(b+12);
	if (size > n - 22 - Vars || nobj < 1 || nobj > Nobj)
		return -1;
	s = b + 22;
	v = s + size;
	hq = hqr("SCENE.HQR", scene, &hqsize);
	if (hq == NULL) {
		*why = "its upgrade reads SCENE.HQR and ANIM.HQR: -g names the game's directory";
		return -1;
	}
	if (hqsize != size || crc32(0, hq, hqsize) != get32(b+8)) {
		free(hq);
		*why = "its scene is another SCENE.HQR's";
		return -1;
	}
	np = places(scene, &pl, &psize);
	nz = zones(s, size, &z);
	if (nz < 0 || nz > Nzones) {
		free(hq);
		*why = "its scene's data reads past its end";
		return -1;
	}
	keep = emalloc(size);
	memset(keep, 0, size);
	for (i = 0; i < np; i++)
		for (k = 0; k < pl[i].Size && pl[i].Offset + k < size; k++)
			keep[pl[i].Offset + k] = 1;
	for (i = 0; i < nz; i++)
		memset(keep + z + 24*i + 18, 1, 6);
	for (i = 0; i < size; i++)
		if (s[i] != hq[i] && !keep[i])
			break;
	free(keep);
	free(hq);
	if (i < size) {
		*why = "its scene's data differs from SCENE.HQR's outside the places today's save keeps";
		return -1;
	}

	memset(black, 0, sizeof black);
	bput(o, black, Npic);
	bput(o, v, 80);	/* ListFlagCube */
	bbyte(o, s16(v+80));	/* NbLittleKeys */
	for (i = 0; i < 3; i++)
		blong(o, get32(v + 128 + 4*i));	/* StartXCube, Y, Z */
	blong(o, get32(b+14));	/* the timer */
	bbyte(o, s16(v+124));	/* NumObjFollow */
	bbyte(o, s16(v+92));	/* SaveComportement */
	bword(o, s16(v+94));	/* SaveBeta */
	blong(o, get32(b+8));	/* the scene's CRC-32 */
	blong(o, s16(v+122));	/* LastMyFire */
	blong(o, s16(v+120));	/* LastMyJoy */
	blong(o, s16(v+118));	/* LastJoyFlag */
	bbyte(o, s16(v+84));	/* Bulle */
	bbyte(o, s16(v+116));	/* ActionNormal */
	blong(o, -1);	/* InventoryAction, as the main loop leaves it */
	blong(o, s16(v+86));	/* MagicBall */
	bbyte(o, s16(v+88));	/* MagicBallType */
	bword(o, s16(v+90));	/* MagicBallCount */
	bbyte(o, s16(v+110));	/* FlagClimbing */
	blong(o, s16(v+96));	/* StartYFalling */
	bput(o, v+98, 10);	/* RealFalling */
	blong(o, s16(v+108));	/* StepFalling */
	bbyte(o, s16(v+126));	/* CameraZone */
	blong(o, 0);	/* InvSelect, the inventory's first */
	bword(o, s16(v+114));	/* NumPingouin */
	blong(o, nobj);
	p = 22 + size + Vars;
	for (i = 0; i < nobj; i++) {
		if (n - p < Block + 5)
			return -1;
		a = b + p;
		kind = b[p + Block + 4];
		p += Block + 5;
		has = 0;
		key = Keynone;
		keytime = 0;
		switch (kind) {
		case 0:
			break;
		case 2:
			if (n - p < 2)
				return -1;
			has = s16(b+p) != -1;
			p += 2;
			break;
		case 1:
			if (n - p < 13)
				return -1;
			where = b[p];
			off = get32(b+p+1);
			keytime = get32(b+p+5);
			g = get32(b+p+9);
			if (g < 0 || n - p - 13 < g)
				return -1;
			p += 13 + g;
			has = 1;
			if (where == 0)
				key = Keynone;
			else if (where == 2)
				key = keyframe(s16(a+98), off);
			else
				key = Keyblend;
			if (key == -3) {
				*why = "its upgrade reads ANIM.HQR: -g names the game's directory";
				return -1;
			}
			break;
		default:
			*why = "an actor's body is out of its range";
			return -1;
		}
		actor(o, a, has, key, keytime);
	}
	blong(o, np);
	blong(o, psize);
	for (i = 0; i < np; i++) {
		if (pl[i].Offset + pl[i].Size > size) {
			*why = "its scene is shorter than its places";
			return -1;
		}
		bput(o, s + pl[i].Offset, pl[i].Size);
	}
	bbyte(o, Nextras);
	bput(o, v+938, Nextras*Extra);
	blong(o, nz);
	for (i = 0; i < nz; i++)
		for (j = 0; j < 3; j++)
			blong(o, s16(s + z + 24*i + 18 + 2*j));	/* Info1 to Info3 */
	for (ni = 0, i = 0; i < Nincrust; i++)
		if (s16(v + 2638 + Incrust*i) != -1)
			ni++;
	bbyte(o, ni);
	for (i = 0; i < Nincrust; i++)
		if (s16(v + 2638 + Incrust*i) != -1)
			bput(o, v + 2638 + Incrust*i, Incrust);
	for (i = 0; i < 3; i++)
		blong(o, get32(v + 140 + 4*i));	/* WorldXCube, Y, Z */
	bword(o, s16(v+82));	/* GameChoice */
	bword(o, s16(v+112));	/* FlagWater */
	bword(o, s16(v+934));	/* TextLig */
	bword(o, s16(v+936));	/* BetaUsedObj */
	bword(o, s16(v+152));	/* ZoneGrm */
	bword(o, s16(v+154));	/* IndexGrm */
	bword(o, s16(v+156));	/* FlagPalettePcx */
	bput(o, v+158, 768);	/* PalettePcx */
	bword(o, s16(v+926));	/* SamplePlayed */
	blong(o, get32(v+928));	/* TimerNextAmbiance */
	bword(o, s16(v+932));	/* BigSampleRepeat */
	bword(o, -1);	/* MemoComportement, empty */
	*why = "its snapshot is cut short";
	if (n - p < 4 + Animbuf)
		return -1;
	p += 4 + Animbuf;	/* BufferAnim, which today's load makes again */
	if (runs(o, b, n - 4, &p) < 0 || n - 4 - p < 4)
		return -1;
	p += 4;	/* the music, the scene's own starting at the load */
	if (loops(o, b, n - 4, &p) < 0)
		return -1;
	if (n - 4 - p != 0) {
		*why = "its snapshot holds bytes past its end";
		return -1;
	}
	bput(o, b+18, 4);	/* the seed */
	return 0;
}

/* The bytes of file path, at most Nfile, or NULL with why. */
static uint8_t*
readfile(char *path, long *n, char **why)
{
	FILE *f;
	uint8_t *b;

	f = fopen(path, "rb");
	if (f == NULL) {
		*why = "cannot be opened";
		return NULL;
	}
	b = emalloc(Nfile + 1);
	*n = fread(b, 1, Nfile + 1, f);
	if (ferror(f)) {
		*why = "cannot be read";
		free(b);
		b = NULL;
	} else if (*n > Nfile) {
		*why = "is larger than any save";
		free(b);
		b = NULL;
	}
	fclose(f);
	return b;
}

/* The scene the record holds. */
static int
scene(Save *s)
{
	return s->b[s->name + 2 + Nflags];
}

/*
 * The tail after a version 16 or 17's record, in today's form, or why
 * it was dropped. Today's is kept, its CRC-32 right and its context
 * walked through. Today's written before it ended with its CRC-32 is
 * walked to its very end, the CRC-32 appended. LBA2's first form,
 * version 17, is made today's, and a snapshot kept for prepare.
 */
static void
tail(Save *s)
{
	Buf o;
	Walk w;
	uint8_t *t, *c;
	long after, size;
	char *why;

	after = s->n - s->rec;
	c = s->b + s->rec;
	if (s->version == Vport && !(s->b[0] & Packed) && after >= 8 && memcmp(c, "SNAP", 4) == 0) {
		s->form = "snapshot";
		size = get32(c+4);
		if (size < 4 || size != after - 8) {
			s->why = "its snapshot's size is the file's";
			return;
		}
		if (crc32(0, c+8, size-4) != get32(c+8+size-4)) {
			s->why = "its snapshot's CRC-32 differs";
			return;
		}
		s->snap = c+8;
		s->nsnap = size;
		return;
	}
	s->form = s->version == Vport ? "16" : "17";
	if (s->b[0] & Packed) {
		if (after < 4) {
			s->why = "it is cut short in its size";
			return;
		}
		size = get32(c);
		if (size <= Npic || size > Ntail || after - 4 >= size) {
			s->why = "its packed size is out of its range";
			return;
		}
		t = emalloc(size);
		if (expand(c+4, after-4, t, size, 1) < 0) {
			free(t);
			s->why = "its packed stream is damaged";
			return;
		}
	} else {
		size = after;
		if (size <= Npic || size >= Ntail) {
			s->why = after == 0 ? "it holds none" : "its size is out of its range";
			return;
		}
		t = emalloc(size);
		memcpy(t, c, size);
	}
	if (s->version == Vlba2) {
		memset(&o, 0, sizeof o);
		if (from17(t, size, scene(s), &o, &why) < 0 || o.cut) {
			free(o.p);
			free(t);
			s->why = o.cut ? "a value is out of today's width" : why;
			return;
		}
		free(t);
		blong(&o, crc32(0, o.p, o.n));
		s->tail = o.p;
		s->ntail = o.n;
		s->pic = 1;
		return;
	}
	if (size > Npic + 4 && crc32(0, t, size-4) == get32(t+size-4)) {
		if (walk(t+Npic, size-Npic-4, scene(s), &w, &why) < 0) {
			free(t);
			s->why = why;
			return;
		}
		s->tail = t;
		s->ntail = size;
		s->pic = 1;
		return;
	}
	if (walk(t+Npic, size-Npic, scene(s), &w, &why) < 0) {
		free(t);
		s->why = "its CRC-32 differs";
		return;
	}
	s->form = "16 before its CRC-32";
	t = realloc(t, size + 4);
	if (t == NULL)
		fatal("out of memory");
	put32(t+size, crc32(0, t, size));
	s->tail = t;
	s->ntail = size + 4;
	s->pic = 1;
}

static void
freesave(Save *s)
{
	free(s->b);
	free(s->tail);
	memset(s, 0, sizeof *s);
}

/* The save in file path read into s: 0, or -1 with why. */
static int
readsave(Save *s, char *path, char **why)
{
	long p, i, after;

	memset(s, 0, sizeof *s);
	s->path = path;
	s->b = readfile(path, &s->n, why);
	if (s->b == NULL)
		return -1;
	if (s->n < 2) {
		*why = "is no save: it is cut short";
		return -1;
	}
	for (p = 1; p < s->n && p <= Nname + 1 && s->b[p] != 0; p++)
		;
	if (p > Nname + 1 || p >= s->n) {
		*why = p >= s->n ? "is no save: it is cut short in the name" : "is no save: its name runs past 29 characters";
		return -1;
	}
	s->name = p;
	p++;
	for (i = 0; i < (long)(sizeof parts / sizeof parts[0]); i++) {
		if (parts[i].count != 0) {
			if (p >= s->n || s->b[p] != parts[i].count) {
				snprintf(whybuf, sizeof whybuf, "is no save: %s %s", parts[i].what,
					p >= s->n ? "are missing" : "count another number than every version writes");
				*why = whybuf;
				return -1;
			}
			p += 1 + parts[i].count;
		} else {
			p += parts[i].n;
		}
		if (p > s->n) {
			snprintf(whybuf, sizeof whybuf, "is no save: it is cut short in %s", parts[i].what);
			*why = whybuf;
			return -1;
		}
	}
	s->rec = p;
	for (i = 0; i < Nflags; i++)
		if (s->b[s->name + 2 + i] & Mark)
			s->marks = 1;
	s->version = s->b[0];
	if (s->version == (Vport|Packed) || s->version == (Vlba2|Packed))
		s->version &= ~Packed;
	after = s->n - s->rec;
	switch (s->version) {
	case Vpc:
		s->extra = after;
		break;
	case Vdotemu:
		s->hastime = 1;
		for (i = 0; i < Ntime && i < after; i++)
			s->time |= (long long)s->b[s->rec + i] << (8*i);
		s->extra = after > Ntime ? after - Ntime : 0;
		break;
	case Vport:
	case Vlba2:
		tail(s);
		break;
	default:
		s->extra = after;
		break;
	}
	return 0;
}

/*
 * The tail of s for -t legacy, a snapshot upgraded, which needs the
 * game's files: 0, or -1 with why.
 */
static int
prepare(Save *s, char **why)
{
	Buf o;

	if (s->snap == NULL || s->tail != NULL)
		return 0;
	memset(&o, 0, sizeof o);
	if (fromsnap(s->snap, s->nsnap, scene(s), &o, why) < 0 || o.cut) {
		if (o.cut)
			*why = "a value is out of today's width";
		free(o.p);
		return -1;
	}
	blong(&o, crc32(0, o.p, o.n));
	s->tail = o.p;
	s->ntail = o.n;
	return 0;
}

/* Whether a and b are the same name, ASCII letters' case aside, as stricmp compares them. */
static int
samename(char *a, char *b)
{
	for (; *a != 0 && *b != 0; a++, b++)
		if (*a != *b && !((*a|0x20) == (*b|0x20) && (*a|0x20) >= 'a' && (*a|0x20) <= 'z'))
			return 0;
	return *a == *b;
}

/* The last part of a path. */
static char*
base(char *path)
{
	char *p;

	for (p = path + strlen(path); p > path; p--)
		if (p[-1] == '/' || p[-1] == '\\')
			break;
	return p;
}

/* Whether name ends in .LBA, whatever its case. */
static int
lbaname(char *name)
{
	size_t n;

	n = strlen(name);
	return n > 4 && samename(name + n - 4, ".LBA");
}

static int
isdir(char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Whether dir holds a file named name, whatever its case. */
static int
taken(char *dir, char *name)
{
	struct dirent *e;
	DIR *d;
	int found;

	d = opendir(dir);
	if (d == NULL)
		return 0;
	found = 0;
	while (!found && (e = readdir(d)) != NULL)
		found = samename(e->d_name, name);
	closedir(d);
	return found;
}

/* The save of dir whose player is named name, or NULL. */
static char*
player(char *dir, char *name)
{
	static char path[Npath];
	struct dirent *e;
	DIR *d;
	FILE *f;
	char other[Nname + 2];
	size_t n;

	d = opendir(dir);
	if (d == NULL)
		return NULL;
	while ((e = readdir(d)) != NULL) {
		if (!lbaname(e->d_name))
			continue;
		if (format(path, sizeof path, "%s/%s", dir, e->d_name) < 0)
			continue;
		f = fopen(path, "rb");
		if (f == NULL)
			continue;
		n = fread(other, 1, sizeof other, f);
		fclose(f);
		if (n < 2 || memchr(other + 1, 0, n - 1) == NULL)
			continue;
		if (samename(other + 1, name)) {
			closedir(d);
			return path;
		}
	}
	closedir(d);
	return NULL;
}

/*
 * The name of a new save of s in dir, as v names one: the file's own
 * number or date when free, else the lowest free number, or the
 * file's time a second later while taken. -1 if none is free.
 */
static int
newname(char *dir, Save *s, Version *v, char *name, size_t n)
{
	struct stat st;
	struct tm *tm;
	time_t t;
	char *b, *e;
	long k;

	b = base(s->path);
	if (v->dated) {
		if (strlen(b) == 18 && strspn(b, "0123456789") == 14 && lbaname(b)) {
			if (format(name, n, "%.14s.LBA", b) == 0 && !taken(dir, name))
				return 0;
		}
		t = stat(s->path, &st) == 0 ? st.st_mtime : time(NULL);
		for (k = 0; k < Nsaves; k++, t++) {
			tm = localtime(&t);
			if (tm == NULL)
				return -1;
			if (format(name, n, "%04d%02d%02d%02d%02d%02d.LBA", tm->tm_year+1900, tm->tm_mon+1,
				tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec) == 0 && !taken(dir, name))
				return 0;
		}
		return -1;
	}
	if ((b[0] == 'S' || b[0] == 's') && lbaname(b)) {
		k = strtol(b+1, &e, 10);
		if (e > b+1 && e - b == (long)strlen(b) - 4 && k >= 0 && k < Nsaves) {
			if (format(name, n, "S%ld.LBA", k) == 0 && !taken(dir, name))
				return 0;
		}
	}
	for (k = 1; k < Nsaves; k++)
		if (format(name, n, "S%ld.LBA", k) == 0 && !taken(dir, name))
			return 0;
	return -1;
}

/* n bytes written into path, under a name of their own first: 0, or -1. */
static int
writefile(char *path, uint8_t *b, long n)
{
	char tmp[Npath];
	FILE *f;
	int ok;

	if (format(tmp, sizeof tmp, "%s.tmp", path) < 0)
		return -1;
	f = fopen(tmp, "wb");
	if (f == NULL)
		return -1;
	ok = (long)fwrite(b, 1, n, f) == n;
	if (fclose(f) != 0)
		ok = 0;
	if (ok && rename(tmp, path) != 0) {
		remove(path);
		ok = rename(tmp, path) == 0;
	}
	if (!ok)
		remove(tmp);
	return ok ? 0 : -1;
}

/* PNG's chunk of type t, n bytes of data, into o. */
static void
chunk(Buf *o, char *t, uint8_t *data, long n)
{
	uint8_t w[4];
	long start;

	putbe32(w, n);
	bput(o, w, 4);
	start = o->n;
	bput(o, t, 4);
	bput(o, data, n);
	putbe32(w, crc32(0, o->p + start, o->n - start));
	bput(o, w, 4);
}

/*
 * LBA1 Legacy's picture, Picx by Picy in the game's palette, doubled
 * to 320x240 in RGB as DotEmu's picture of a slot, a PNG whose
 * deflate blocks are stored: 0, or -1.
 */
static int
writepng(char *path, uint8_t *pic, uint8_t *pal)
{
	static uint8_t sig[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	Buf raw, z, o;
	uint8_t hdr[13], row[1 + 3*2*Picx], blk[5], w[4];
	uint32_t a, b;
	long x, y, i, k;
	int ret;

	memset(&raw, 0, sizeof raw);
	for (y = 0; y < 2*Picy; y++) {
		row[0] = 0;
		for (x = 0; x < 2*Picx; x++)
			memcpy(row + 1 + 3*x, pal + 3*pic[(y/2)*Picx + x/2], 3);
		bput(&raw, row, sizeof row);
	}
	memset(&z, 0, sizeof z);
	bput(&z, "\x78\x01", 2);
	for (i = 0; i < raw.n; i += k) {
		k = raw.n - i < 65535 ? raw.n - i : 65535;
		blk[0] = i + k == raw.n;
		blk[1] = k;
		blk[2] = k>>8;
		blk[3] = ~k;
		blk[4] = (~k)>>8;
		bput(&z, blk, 5);
		bput(&z, raw.p + i, k);
	}
	a = 1;
	b = 0;
	for (i = 0; i < raw.n; i++) {
		a = (a + raw.p[i]) % 65521;
		b = (b + a) % 65521;
	}
	putbe32(w, b<<16 | a);
	bput(&z, w, 4);
	memset(&o, 0, sizeof o);
	bput(&o, sig, sizeof sig);
	putbe32(hdr, 2*Picx);
	putbe32(hdr+4, 2*Picy);
	hdr[8] = 8;	/* bits */
	hdr[9] = 2;	/* RGB */
	hdr[10] = hdr[11] = hdr[12] = 0;
	chunk(&o, "IHDR", hdr, sizeof hdr);
	chunk(&o, "IDAT", z.p, z.n);
	chunk(&o, "IEND", NULL, 0);
	ret = writefile(path, o.p, o.n);
	free(raw.p);
	free(z.p);
	free(o.p);
	return ret;
}

/*
 * The save s as version v writes it, named name, into path: 0, or -1.
 * What v drops is said.
 */
static int
convert(Save *s, Version *v, char *name, char *path)
{
	Buf o;
	uint8_t c, t[Ntime], *pal, *packed;
	char png[Npath];
	unsigned long n;
	long i, size;
	int own, ret;

	memset(&o, 0, sizeof o);
	c = v->version;
	own = v->tail && s->tail != NULL;
	if (own)
		c = Vport;
	bput(&o, &c, 1);
	bput(&o, name, strlen(name) + 1);
	bput(&o, s->b + s->name + 1, s->rec - s->name - 1);
	if (!v->marks)
		for (i = 0; i < Nflags; i++)
			o.p[strlen(name) + 3 + i] &= ~Mark;
	if (v->time) {
		for (i = 0; i < Ntime; i++)
			t[i] = s->time >> (8*i);
		bput(&o, t, Ntime);
	}
	if (own) {
		if (samename(base(path), "autosave.lba") || samename(base(path), "current.lba")) {
			bput(&o, s->tail, s->ntail);
		} else {
			packed = emalloc(s->ntail + 64);
			n = Compress_LZSS((char*)s->tail, (char*)packed, s->ntail);
			if ((long)n < s->ntail) {
				o.p[0] |= Packed;
				put32(t, s->ntail);
				bput(&o, t, 4);
				bput(&o, packed, n);
			} else {
				bput(&o, s->tail, s->ntail);
			}
			free(packed);
		}
	}
	ret = writefile(path, o.p, o.n);
	free(o.p);
	if (ret < 0)
		return -1;
	if (v->time && s->pic && s->tail != NULL && gamedir != NULL) {
		pal = hqr("RESS.HQR", 0, &size);
		if (format(png, sizeof png, "%s.png", path) < 0 || pal == NULL || size < 768
		|| writepng(png, s->tail, pal) < 0)
			note(s->path, "%s cannot be written: the slot shows DotEmu's empty frame", png);
		free(pal);
	}
	if (s->marks && !v->marks)
		note(s->path, "New Game+ dropped: the Classic alone keeps it");
	if (s->hastime && s->time != 0 && !v->time)
		note(s->path, "the play time dropped: DotEmu alone keeps it");
	if ((s->tail != NULL || s->snap != NULL) && !v->tail)
		note(s->path, "the scene as it ran dropped: the save loads at its scene's entry");
	if (s->why != NULL)
		note(s->path, "the scene as it ran dropped, %s: the save loads at its scene's entry", s->why);
	if (s->extra > 0)
		note(s->path, "%ld bytes after the record dropped", s->extra);
	return 0;
}

/* -l: s's fields, a line each. */
static void
list(Save *s)
{
	Walk w;
	uint8_t *r, *hq;
	long i, size;
	char *why, *sep;

	r = s->b + s->name + 1;
	printf("file\t%s\n", s->path);
	printf("version\t%d%s\n", s->version, s->b[0] & Packed && (s->version == Vport || s->version == Vlba2) ? " packed" : "");
	printf("name\t%s\n", (char*)s->b + 1);
	r += 1 + Nflags;
	printf("scene\t%d\n", r[0]);
	printf("chapter\t%d\n", r[1]);
	printf("behaviour\t%d\n", r[2]);
	printf("life\t%d\n", r[3]);
	printf("gold\t%ld\n", (long)get16(r+4));
	printf("magic\t%d %d\n", r[6], r[7]);
	printf("cloverboxes\t%d\n", r[8]);
	printf("entry\t%ld %ld %ld\n", s16(r+9), s16(r+11), s16(r+13));
	printf("beta\t%ld\n", s16(r+15));
	printf("body\t%d\n", r[17]);
	printf("fuel\t%d\n", r[18 + 1 + Nholo]);
	printf("clovers\t%d\n", r[18 + 1 + Nholo + 1 + 1 + Ninv]);
	printf("weapon\t%ld\n", s16(r + 18 + 1 + Nholo + 1 + 1 + Ninv + 1));
	printf("flags\t");
	sep = "";
	for (i = 0; i < Nflags; i++)
		if (s->b[s->name + 2 + i] != 0) {
			printf("%s%ld=%d", sep, i, s->b[s->name + 2 + i]);
			sep = " ";
		}
	printf("\n");
	printf("inventory\t");
	sep = "";
	for (i = 0; i < Ninv; i++)
		if (r[18 + 1 + Nholo + 1 + 1 + i] != 0) {
			printf("%s%ld", sep, i);
			sep = " ";
		}
	printf("\n");
	printf("newgameplus\t%s\n", s->marks ? "yes" : "no");
	if (s->hastime)
		printf("time\t%lld\n", s->time);
	if (s->extra > 0)
		printf("extra\t%ld\n", s->extra);
	if (s->form != NULL)
		printf("tail\t%s\n", s->form);
	if (s->why != NULL)
		printf("damaged\t%s\n", s->why);
	if (gamedir != NULL && prepare(s, &why) < 0)
		printf("damaged\t%s\n", why);
	if (s->tail != NULL && walk(s->tail + Npic, s->ntail - Npic - 4, scene(s), &w, &why) == 0) {
		printf("actors\t%ld\n", w.nobj);
		printf("hero\t%ld %ld %ld\n", w.hero[0], w.hero[1], w.hero[2]);
		printf("timer\t%lu\n", (unsigned long)w.timer);
		printf("patches\t%ld\n", w.npatch);
		printf("zones\t%ld\n", w.nzones);
		printf("overlays\t%ld\n", w.nincrust);
		printf("runs\t%ld\n", w.runs);
		printf("loops\t%ld\n", w.loops);
		printf("scenecrc\t%08lx", (unsigned long)w.checksum);
		hq = hqr("SCENE.HQR", scene(s), &size);
		if (hq != NULL)
			printf(" %s", crc32(0, hq, size) == w.checksum ? "SCENE.HQR's" : "another SCENE.HQR's");
		free(hq);
		printf("\n");
	}
	printf("\n");
}

int
main(int argc, char **argv)
{
	Version *v;
	Save s;
	char *name, *dest, *why, *end, *who, *other, file[64], out[Npath];
	long long seconds;
	int i, l, todir, settime, own;

	v = NULL;
	name = NULL;
	l = 0;
	settime = 0;
	seconds = 0;
	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != 0; i++) {
		if (strcmp(argv[i], "--") == 0) {
			i++;
			break;
		}
		if (strcmp(argv[i], "-l") == 0) {
			l = 1;
		} else if (strcmp(argv[i], "-t") == 0 && i+1 < argc) {
			for (v = versions; v < versions + sizeof versions / sizeof versions[0]; v++)
				if (strcmp(v->name, argv[i+1]) == 0)
					break;
			if (v == versions + sizeof versions / sizeof versions[0])
				usage();
			i++;
		} else if (strcmp(argv[i], "-n") == 0 && i+1 < argc) {
			name = argv[++i];
		} else if (strcmp(argv[i], "-T") == 0 && i+1 < argc) {
			seconds = strtoll(argv[++i], &end, 10);
			if (*end != 0 || argv[i][0] == 0 || seconds < 0 || seconds > Maxtime)
				usage();
			settime = 1;
		} else if (strcmp(argv[i], "-g") == 0 && i+1 < argc) {
			gamedir = argv[++i];
		} else {
			usage();
		}
	}
	argc -= i;
	argv += i;
	if (l) {
		if (v != NULL || name != NULL || settime || argc < 1)
			usage();
		for (i = 0; i < argc; i++) {
			if (readsave(&s, argv[i], &why) < 0)
				complain(argv[i], "%s", why);
			else
				list(&s);
			freesave(&s);
		}
		return status;
	}
	if (v == NULL || argc < 2 || (settime && !v->time))
		usage();
	dest = argv[argc-1];
	argc--;
	todir = isdir(dest);
	if (!todir && (argc > 1 || !lbaname(dest)))
		usage();
	if (name != NULL) {
		if (argc > 1 || strlen(name) < 1 || strlen(name) > Nname)
			usage();
		for (end = name; *end != 0; end++)
			if ((uint8_t)*end < 0x20 || (uint8_t)*end >= 0x7f)
				usage();
	}
	for (i = 0; i < argc; i++, freesave(&s)) {
		if (readsave(&s, argv[i], &why) < 0) {
			complain(argv[i], "%s", why);
			continue;
		}
		if (v->tail && prepare(&s, &why) < 0) {
			complain(argv[i], "its scene as it ran, a snapshot, cannot be upgraded: %s", why);
			continue;
		}
		if (settime)
			s.time = seconds;
		if (todir) {
			if (newname(dest, &s, v, file, sizeof file) < 0) {
				complain(argv[i], "%s holds every name a save takes", dest);
				continue;
			}
			if (format(out, sizeof out, "%s/%s", dest, file) < 0) {
				complain(argv[i], "%s/%s is too long a path", dest, file);
				continue;
			}
		} else if (format(out, sizeof out, "%s", dest) < 0) {
			complain(argv[i], "%s is too long a path", dest);
			continue;
		}
		who = name != NULL ? name : (char*)s.b + 1;
		own = v->tail && (samename(base(out), "autosave.lba") || samename(base(out), "current.lba"));
		if (own && name != NULL) {
			complain(argv[i], "%s holds LBA1 Legacy's own save: -n names a player's", base(out));
			continue;
		}
		if (own) {
			who = samename(base(out), "autosave.lba") ? "AUTOSAVE" : "CURRENT";
		} else if (v->tail && (samename(who, "AUTOSAVE") || samename(who, "CURRENT"))) {
			complain(argv[i], "LBA1 Legacy takes %s for its own save: name it with -n", who);
			continue;
		}
		if ((long)strlen(who) > v->maxname) {
			complain(argv[i], "its name has %d characters, where %s's list reads %d: name it with -n",
				(int)strlen(who), v->name, v->maxname);
			continue;
		}
		if (todir && v->names && (other = player(dest, who)) != NULL) {
			complain(argv[i], "%s is the player %s's already: name it with -n", other, who);
			continue;
		}
		if (convert(&s, v, who, out) < 0) {
			complain(argv[i], "%s cannot be written", out);
			continue;
		}
		printf("%s\n", out);
	}
	return status;
}
