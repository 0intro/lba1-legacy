#include <dirent.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "abi.h"
#include "asmoracle.h"

/*
 * asmoracle: the game's assembly, as a DOS build of the tree links it,
 * against its translations into C.
 *
 *	asmoracle [-g gamedir] [-x] lba0.elf lba0.sym [test...]
 *
 * lba0.elf is a DOS build's LBA0.EXE (make OS=dos ARCH=386
 * CC=openwatcom or CC=watcom) as tools/le2elf.py makes it, mapped where
 * it was linked, its functions and data named by lba0.sym: its assembly
 * is the tree's, LBA.EXE's byte for byte or by equivalent encodings.
 * Ours are the translations, compiled in. Each test calls a module's
 * functions in both on the same inputs and state, and compares what
 * they return, write and leave, the calling convention checked (abi.c).
 * gamedir holds the game's files, which the tests that read them then
 * take too: the packed entries of its HQR files, its bodies and
 * animations, its movies and its bricks. Without it the tests run on
 * data of their own. -x makes a test exhaustive where it can be.
 *
 * The tests (all of them without arguments):
 *
 *	sqr	Sqr on every number below 2^24, the edges and as many random
 *		ones (with -x, on every 32-bit number), the assembly called
 *		through its EAX
 *	expand	LZS streams of random data, literals and copies of every
 *		length and distance, and with gamedir every packed entry of
 *		the game's HQR files: the same bytes, nothing written past them
 *	pfunc	p_func's angles, rotations and distances of random vectors,
 *		small to past what fits shifted by 14, rules of three, values
 *		and angles over time with the clock moved between
 *	trigo	p_trigo's procedures in random order on one state, a million
 *		calls
 *	anim	p_anim's procedures, a million calls on random bodies and
 *		animations, and with gamedir the game's (BODY.HQR, ANIM.HQR),
 *		in an arena for each side, the arenas compared whole every
 *		1000 calls
 *	line, box, block, screen, graph, mask, font
 *		the drawing modules, on a screen for each side: a million
 *		lines in and out of random clip boxes, then boxes, blocks copied
 *		between Log and Screen or within one, random graphs, masks
 *		made by both sides' CalcGraphMsk, strings in a random font,
 *		and the screen cleared and copied
 *	fla	adfli_a's frames: 10000 random frames of the movies' kinds,
 *		runs of 127 at most, and with gamedir every frame of the
 *		game's movies
 *	cpymask	CopyMask through random masks, 127 wide or 255, from Screen
 *		or from another screen, and with gamedir through the masks of
 *		the game's bricks (LBA_BRK.HQR) as DrawOverBrick calls it, and
 *		with -x one-line masks of every sequence of up to 6 runs 8
 *		wide at most and of the runs about 128 and 255, through every
 *		clip box on their run boundaries
 *	fire	the menus' fire, 10000 steps at random lines and colours, from
 *		a random fire, a cold one or one cold but for a few hot points
 *	func	SmallSort, ShadeBox and CopyBlockMCGA: lists of entries of
 *		several sizes with repeated keys, boxes in and out of random
 *		clip boxes, and boxes of the screen copied 320 bytes a line
 *	poly	s_poly's polygons and circles and s_fillv's fills, a million:
 *		convex polygons facing either way, every type at every level,
 *		in and out of random clip boxes, and past the game's inputs W
 *		shapes, boxes empty across x or a side 65536 off, types from
 *		8000h, circles of radius past 32767, then 13 polygons at the
 *		ends of 16 bits: edges over 32767 lines or columns across a
 *		side, edges of 32768 columns and ending left of x 0, points
 *		at 32767 and at -32768
 *	texture	texture's triangles, textured and Gouraud, its fillers,
 *		whole, clipped and darkened, and its edges stepped into words,
 *		a million rounds, past the game's inputs triangles with a point
 *		left of x 0 or on line -1, edges 100000 out and, for the
 *		clipped fillers, lines from left of x 0 and boxes 65536 on
 *	obj	p_ob_iso's PatchObjet and AffObjetIso, 10000 drawings at random
 *		positions, angles and poses, in isometry or in perspective,
 *		under random lights and clip boxes, of random bodies with
 *		groups and without, and with gamedir of the game's (BODY.HQR,
 *		INVOBJ.HQR) in half of them, then 29 at the projection's
 *		edges: 25 of a line, behind the camera, on its plane, past 16
 *		bits deep and wide, and at the box's first bounds, 3 of a
 *		sphere whose LFactorX, radius or quotient passes 16 bits and
 *		one of faces deeper than 32767, then 1000 of bodies of points
 *		off x 0 without groups, and 2 of faces lit past the game's
 *		lights
 *	grille	grille_a's routines: a random scene of blocks, positions in,
 *		on the edges of and outside the map, random packed columns
 *	small	balance, buffer_a, hqr and p_sintab: Balance over every
 *		balance, MovMem, RazMem and CompBuf on random buffers,
 *		HQR_GiveIndex on lists holding each index once or repeated,
 *		and the sine table
 *
 * A test fails when ours and the assembly differ.
 */

typedef struct Sym Sym;
typedef struct Test Test;

/* a name of lba0.sym */
struct Sym
{
	uint32_t	va;
	char	*name;
};

struct Test
{
	char	*name;
	void	(*fn)(void);
};

uint32_t	Sqr(uint32_t);
void	Expand(void*, void*, uint32_t);

static Sym *syms;
static int nsyms;
static int nfail;
int exhaustive;
char *gamedir;
static uint32_t seed = 1;

void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "asmoracle: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

void
fail(char *fmt, ...)
{
	va_list arg;

	if (nfail++ >= 20)
		return;
	va_start(arg, fmt);
	fprintf(stderr, "asmoracle: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
}

/* xorshift32 */
uint32_t
rnd(void)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

uint32_t
rndn(uint32_t n)
{
	return rnd() % n;
}

/* A number from a to b. */
int32_t
rndr(int32_t a, int32_t b)
{
	return a + (int32_t)rndn(b-a+1);
}

uint32_t
get16(uint8_t *p)
{
	return p[0] | p[1]<<8;
}

uint32_t
get32(uint8_t *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24;
}

unsigned char*
readfile(char *path, long *np)
{
	FILE *f;
	unsigned char *b;
	long n;

	f = fopen(path, "rb");
	if (f == NULL)
		fatal("%s: cannot open", path);
	if (fseek(f, 0, SEEK_END) < 0)
		fatal("%s: cannot seek", path);
	n = ftell(f);
	if (n < 0 || fseek(f, 0, SEEK_SET) < 0)
		fatal("%s: cannot seek", path);
	b = malloc(n > 0 ? n : 1);
	if (b == NULL || fread(b, 1, n, f) != (size_t)n)
		fatal("%s: cannot read", path);
	fclose(f);
	*np = n;
	return b;
}

/* Map the PT_LOAD segments of an ELF32 at their addresses. */
static void
load(char *path)
{
	unsigned char *b, *ph;
	uint32_t phoff, off, va, filesz, memsz, lo, hi;
	long n;
	int i, phnum;
	void *m;

	b = readfile(path, &n);
	if (n < 52 || memcmp(b, "\177ELF\001\001", 6) != 0 || get16(b+18) != 3)
		fatal("%s: not an ELF32 i386 file", path);
	phoff = get32(b+28);
	phnum = get16(b+44);
	if (phoff > (uint32_t)n || (uint32_t)phnum > ((uint32_t)n - phoff) / 32)
		fatal("%s: bad program header", path);
	for (i = 0; i < phnum; i++) {
		ph = b + phoff + 32*i;
		if (get32(ph) != 1)
			continue;
		off = get32(ph+4);
		va = get32(ph+8);
		filesz = get32(ph+16);
		memsz = get32(ph+20);
		if (filesz > memsz || off > (uint32_t)n || filesz > (uint32_t)n - off)
			fatal("%s: bad segment", path);
		lo = va & ~0xfffu;
		hi = (va + memsz + 0xfff) & ~0xfffu;
		m = mmap((void*)(uintptr_t)lo, hi - lo, PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
		if (m == MAP_FAILED || m != (void*)(uintptr_t)lo)
			fatal("cannot map %s's segments where they belong", path);
		memcpy((void*)(uintptr_t)va, b + off, filesz);
	}
	free(b);
}

/* Read a symbol map: lines of address, type and name. */
static void
loadsyms(char *path)
{
	FILE *f;
	char line[512], type[8], name[256];
	unsigned long va;

	f = fopen(path, "r");
	if (f == NULL)
		fatal("%s: cannot open", path);
	while (fgets(line, sizeof line, f) != NULL) {
		if (line[0] == '#' || sscanf(line, "%lx %7s %255s", &va, type, name) != 3)
			continue;
		syms = realloc(syms, (nsyms+1) * sizeof syms[0]);
		if (syms == NULL)
			fatal("%s: out of memory", path);
		syms[nsyms].va = va;
		syms[nsyms].name = strdup(name);
		nsyms++;
	}
	fclose(f);
}

/* The address of one of LBA0.EXE's names. */
uintptr_t
lookup(char *name)
{
	int i;

	for (i = 0; i < nsyms; i++)
		if (strcmp(syms[i].name, name) == 0)
			return syms[i].va;
	fatal("no %s in LBA0.EXE's map", name);
	return 0;
}

/* The assembly's function of n stack arguments, its EAX, the convention checked. */
int32_t
acall(uintptr_t fn, int n, ...)
{
	va_list arg;
	int32_t a[8], r;
	int i;

	va_start(arg, n);
	for (i = 0; i < n; i++)
		a[i] = va_arg(arg, int32_t);
	va_end(arg);
	r = abicall(fn, n, a);
	abiok("the assembly");
	return r;
}

/* Sqr of x by both: 0 if they agree. */
static int
sqr2(uintptr_t asqr, uint32_t x)
{
	Regs r;
	uint32_t c;

	c = Sqr(x);
	memset(&r, 0, sizeof r);
	r.eax = x;
	regcall(asqr, &r);
	if (c == r.eax)
		return 0;
	fail("sqr: Sqr(%#x): ours %#x, the assembly's %#x", x, c, r.eax);
	return -1;
}

static void
testsqr(void)
{
	uintptr_t asqr;
	uint64_t x;
	uint32_t s;
	int k, bad;

	asqr = lookup("Sqr");
	bad = 0;
	if (exhaustive) {
		for (x = 0; x <= 0xffffffffu; x++)
			bad += sqr2(asqr, x) != 0;
		printf("sqr: every 32-bit number, %d differ\n", bad);
		return;
	}
	for (x = 0; x < 1u<<24; x++)
		bad += sqr2(asqr, x) != 0;
	for (k = 0; k < 32; k++)
		for (s = 0; s < 3; s++)
			bad += sqr2(asqr, (1u<<k) - 1 + s) != 0;
	for (s = 1 << 12; s < 1<<16; s++) {
		bad += sqr2(asqr, s*s) != 0;
		bad += sqr2(asqr, s*s - 1) != 0;
	}
	bad += sqr2(asqr, 0xffffffffu) != 0;
	for (k = 0; k < 1<<24; k++)
		bad += sqr2(asqr, rnd()) != 0;
	printf("sqr: below 2^24, the edges and 2^24 random numbers, %d differ\n", bad);
}

enum
{
	Slack = 64,	/* bytes past an expanded entry, each side's own, compared too */
	Maxlzs = 1 << 16,	/* a random stream's bytes at most */
};

/* Expand n bytes of packed by both: 0 if they write the same bytes. */
static int
expand2(uintptr_t aexpand, uint8_t *packed, uint32_t n, char *what)
{
	uint8_t *a, *c;
	int bad;

	a = calloc(1, n+Slack);
	c = calloc(1, n+Slack);
	if (a == NULL || c == NULL)
		fatal("out of memory");
	acall(aexpand, 3, (int32_t)(uintptr_t)packed, (int32_t)(uintptr_t)a, n);
	Expand(packed, c, n);
	bad = memcmp(a, c, n+Slack) != 0;
	if (bad)
		fail("expand: %s differs", what);
	free(a);
	free(c);
	return bad;
}

/*
 * A stream of LZS for n bytes into p, as Expand reads it: a byte of
 * flags, low bit first, for each eight items, a 1 for a literal, a 0 for
 * a copy of 2 to 17 bytes from 1 to 4096 back, within what is already
 * written, the last item ending the count exactly. Its length.
 */
static uint32_t
lzs(uint8_t *p, uint32_t n)
{
	uint8_t *flags;
	uint32_t made, len, back, w;
	int i;

	made = 0;
	len = 0;
	while (made < n) {
		flags = p + len++;
		*flags = 0;
		for (i = 0; i < 8 && made < n; i++) {
			if (made == 0 || n - made < 2 || rndn(3) == 0) {
				*flags |= 1 << i;
				p[len++] = rnd();
				made++;
				continue;
			}
			w = n - made < 17 ? n - made : 17;
			w = rndr(2, w);
			back = made < 4096 ? made : 4096;
			if (rndn(4) != 0 && back > 16)
				back = 16;
			back = rndr(1, back);
			p[len++] = (w - 2) | (back - 1) << 4;
			p[len++] = (back - 1) >> 4;
			made += w;
		}
	}
	return len;
}

/* An HQR file's packed entries: at each offset of its table, the size, the packed size, the method (1, LZS), the data. */
static int
expandhqr(uintptr_t aexpand, char *path, int *bad)
{
	unsigned char *buf;
	char what[1100];
	long n;
	uint32_t i, nent, off, size, packed;
	int count;

	buf = readfile(path, &n);
	count = 0;
	nent = get32(buf) / 4;
	for (i = 0; i < nent && 4*i+4 <= (uint32_t)n; i++) {
		off = get32(buf+4*i);
		if (off == 0 || off > (uint32_t)n || (uint32_t)n - off < 10)
			continue;
		size = get32(buf+off);
		packed = get32(buf+off+4);
		if (get16(buf+off+8) != 1 || size == 0 || packed > (uint32_t)n - off - 10)
			continue;
		snprintf(what, sizeof what, "%s entry %u", path, i);
		*bad += expand2(aexpand, buf+off+10, size, what);
		count++;
	}
	free(buf);
	return count;
}

static void
testexpand(void)
{
	uintptr_t aexpand;
	uint8_t *packed;
	DIR *d;
	struct dirent *e;
	char path[1024], what[64];
	uint32_t n;
	int r, count, bad;

	aexpand = lookup("Expand");
	packed = malloc(2*Maxlzs);
	if (packed == NULL)
		fatal("out of memory");
	bad = 0;
	for (r = 0; r < Rounds/10; r++) {
		n = r < 64 ? (uint32_t)r+1 : 1 + rndn(rndn(4) == 0 ? Maxlzs : 256);
		lzs(packed, n);
		snprintf(what, sizeof what, "a stream of %u bytes", n);
		bad += expand2(aexpand, packed, n, what);
	}
	free(packed);
	printf("expand: %d random streams, %d differ\n", r, bad);
	if (gamedir == NULL)
		return;
	d = opendir(gamedir);
	if (d == NULL)
		fatal("cannot open %s", gamedir);
	count = 0;
	bad = 0;
	while ((e = readdir(d)) != NULL) {
		n = strlen(e->d_name);
		if (n < 4 || strcmp(e->d_name+n-4, ".HQR") != 0)
			continue;
		snprintf(path, sizeof path, "%s/%s", gamedir, e->d_name);
		count += expandhqr(aexpand, path, &bad);
	}
	closedir(d);
	if (count == 0)
		fail("expand: no packed entry in %s", gamedir);
	printf("expand: %d entries of the game's, %d differ\n", count, bad);
}

static Test tests[] = {
	{"sqr", testsqr},
	{"expand", testexpand},
	{"pfunc", testpfunc},
	{"trigo", testtrigo},
	{"anim", testanim},
	{"line", testline},
	{"box", testbox},
	{"block", testblock},
	{"screen", testscreen},
	{"graph", testgraph},
	{"mask", testmask},
	{"font", testfont},
	{"fla", testfla},
	{"cpymask", testcpymask},
	{"fire", testfire},
	{"func", testfunc},
	{"poly", testpoly},
	{"texture", testtexture},
	{"obj", testobj},
	{"grille", testgrille},
	{"small", testsmall},
};

static void
usage(void)
{
	fprintf(stderr, "usage: asmoracle [-g gamedir] [-x] lba0.elf lba0.sym [test...]\n");
	exit(2);
}

/* Whether name is a test's. */
static int
known(char *name)
{
	int i;

	for (i = 0; i < (int)nelem(tests); i++)
		if (strcmp(tests[i].name, name) == 0)
			return 1;
	return 0;
}

/* Whether a test is among those asked, or none is. */
static int
asked(char *name, char **argv, int argc)
{
	int i;

	if (argc == 0)
		return 1;
	for (i = 0; i < argc; i++)
		if (strcmp(argv[i], name) == 0)
			return 1;
	return 0;
}

int
main(int argc, char *argv[])
{
	int i, j;

	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
		if (strcmp(argv[i], "-x") == 0)
			exhaustive = 1;
		else if (strcmp(argv[i], "-g") == 0 && i+1 < argc)
			gamedir = argv[++i];
		else
			usage();
	}
	if (argc - i < 2)
		usage();
	for (j = i + 2; j < argc; j++)
		if (!known(argv[j])) {
			fprintf(stderr, "asmoracle: no test %s\n", argv[j]);
			exit(2);
		}
	load(argv[i]);
	loadsyms(argv[i+1]);
	i += 2;
	for (j = 0; j < (int)nelem(tests); j++)
		if (asked(tests[j].name, argv+i, argc-i))
			tests[j].fn();
	if (nfail > 0) {
		fprintf(stderr, "asmoracle: %d failed\n", nfail);
		return 1;
	}
	printf("asmoracle: ok\n");
	return 0;
}
