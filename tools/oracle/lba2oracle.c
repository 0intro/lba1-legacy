#include <dirent.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * lba2oracle: run LBA2's packer of the saves and its search of the
 * nearest colour against ours.
 *
 *	lba2oracle [-g gamedir] [test...]
 *
 * LBA2's code is built from its sources, every symbol prefixed lba2_
 * (the Makefile): LZSS.CPP's Compress_LZSS, compiled as C, over the
 * tree of strings of COMPRESS.ASM, and FUNC.ASM's SearchBoundColRGB,
 * both assembled by JWasm. Ours are lib386/lib_sys/lzss.c over
 * compress.c, and sources/func.c's SearchBoundColRGB.
 *
 * The tests (both of them without arguments):
 *
 *	pack	every size of input to 64 bytes, of five kinds (random
 *		bytes, a few symbols, runs, repeats, words), 20000 random
 *		inputs of those kinds up to 70000 bytes, and every entry of
 *		the game's HQR files, in gamedir, as it unpacks: the same
 *		size returned and the same bytes written, 64 past them too,
 *		and each packing that shrinks its input expanded back to it
 *		by Expand
 *	colour	a million palettes, random, of a few colours where the
 *		nearest ties, or of 6-bit values, each searched for 64
 *		colours between random bounds, in either order, and for one
 *		over all 256: the same colour
 */

enum
{
	Max = 1 << 21,	/* the largest input */
	Slack = 64,	/* bytes compared past an input's size */
	Shown = 10,	/* differences printed for a test */
};

unsigned long	lba2_Compress_LZSS(char*, char*, unsigned long);
uint8_t	lba2_SearchBoundColRGB(uint32_t, uint32_t, uint32_t, uint8_t*, uint32_t, uint32_t);
unsigned long	Compress_LZSS(char*, char*, unsigned long);
uint8_t	SearchBoundColRGB(uint8_t, uint8_t, uint8_t, uint8_t*, uint8_t, uint8_t);
void	Expand(void*, void*, uint32_t);

/* The screen sources/func.c's other procedures draw on, unused here. */
uint8_t	*Log;
uint32_t	TabOffLine[480];
int32_t	Screen_X;
int32_t	ClipXmin, ClipYmin, ClipXmax, ClipYmax;

static uint8_t in[Max], theirs[Max+Slack], ours[Max+Slack], back[Max];
static uint8_t pal[3*257];
static uint32_t seed;
static int fails, shown;
static char *gamedir = ".";

static void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "lba2oracle: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(1);
}

/* A difference, counted, and printed while few. */
static void
differ(char *fmt, ...)
{
	va_list arg;

	fails++;
	if (++shown > Shown)
		return;
	va_start(arg, fmt);
	fprintf(stderr, "lba2oracle: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
}

static uint32_t
rnd(uint32_t n)
{
	seed = seed*1103515245 + 12345;
	return (seed >> 8) % n;
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

/* The n bytes of in packed by both, then ours expanded back. 1 if it shrank. */
static int
pack(char *what, uint32_t n)
{
	unsigned long t, o;

	memset(theirs, 0xAA, n+Slack);
	memset(ours, 0xAA, n+Slack);
	t = lba2_Compress_LZSS((char*)in, (char*)theirs, n);
	o = Compress_LZSS((char*)in, (char*)ours, n);
	if (t != o || memcmp(theirs, ours, n+Slack) != 0) {
		differ("pack: %s, %u bytes: LBA2's %lu, ours %lu", what, n, t, o);
		return 0;
	}
	if (o >= n)
		return 0;
	memset(back, 0, n);
	Expand(ours, back, n);
	if (memcmp(back, in, n) != 0)
		differ("pack: %s, %u bytes: ours expands otherwise", what, n);
	return 1;
}

/* n bytes of in of a kind: random bytes, a few symbols, runs, repeats, words. */
static void
fill(uint32_t n, uint32_t kind)
{
	static char words[] = "the quick brown fox jumps over the lazy dog ";
	uint32_t i, a;

	a = 1 + rnd(255);
	for (i = 0; i < n; i++) {
		switch (kind) {
		case 0:
			in[i] = rnd(256);
			break;
		case 1:
			in[i] = rnd(a < 4 ? a+1 : 4);
			break;
		case 2:
			in[i] = i > 0 && rnd(8) != 0 ? in[i-1] : rnd(256);
			break;
		case 3:
			in[i] = i >= a && rnd(16) != 0 ? in[i-a] : rnd(256);
			break;
		default:
			in[i] = words[rnd(sizeof words - 1)];
			break;
		}
	}
}

/* Every packed or stored entry of an HQR file, unpacked, packed by both. */
static int
packhqr(char *path, int *shrank)
{
	FILE *f;
	uint8_t *b;
	long n;
	uint32_t i, nent, off, size, stored;
	char what[1100];
	int count;

	f = fopen(path, "rb");
	if (f == NULL)
		fatal("%s: cannot open", path);
	if (fseek(f, 0, SEEK_END) < 0)
		fatal("%s: cannot seek", path);
	n = ftell(f);
	if (n < 4 || fseek(f, 0, SEEK_SET) < 0)
		fatal("%s: cannot seek", path);
	b = malloc(n);
	if (b == NULL || fread(b, 1, n, f) != (size_t)n)
		fatal("%s: cannot read", path);
	fclose(f);
	count = 0;
	nent = get32(b) / 4;
	for (i = 0; i < nent && 4*i+4 <= (uint32_t)n; i++) {
		off = get32(b+4*i);
		if (off == 0 || off+10 > (uint32_t)n)
			continue;
		size = get32(b+off);
		stored = get32(b+off+4);
		if (off+10+stored > (uint32_t)n)
			continue;
		if (size > Max)
			fatal("%s: entry %u of %u bytes, past %d", path, i, size, Max);
		if (get16(b+off+8) == 0)
			memcpy(in, b+off+10, size);
		else
			Expand(b+off+10, in, size);
		snprintf(what, sizeof what, "%s entry %u", path, i);
		*shrank += pack(what, size);
		count++;
	}
	free(b);
	return count;
}

static void
testpack(void)
{
	DIR *d;
	struct dirent *e;
	char what[64], path[1024];
	uint32_t n, kind, r;
	int count, shrank, hqr, len;

	seed = 1;
	shown = 0;
	count = 0;
	shrank = 0;
	for (n = 0; n <= 64; n++) {
		for (kind = 0; kind < 5; kind++) {
			for (r = 0; r < 4; r++) {
				fill(n, kind);
				snprintf(what, sizeof what, "%u bytes of kind %u", n, kind);
				shrank += pack(what, n);
				count++;
			}
		}
	}
	for (r = 0; r < 20000; r++) {
		n = rnd(r%10 == 0 ? 70000 : 3000);
		kind = rnd(5);
		fill(n, kind);
		snprintf(what, sizeof what, "random input %u of kind %u", r, kind);
		shrank += pack(what, n);
		count++;
	}
	d = opendir(gamedir);
	if (d == NULL)
		fatal("cannot open %s", gamedir);
	hqr = 0;
	while ((e = readdir(d)) != NULL) {
		len = strlen(e->d_name);
		if (len < 4 || strcmp(e->d_name+len-4, ".HQR") != 0)
			continue;
		snprintf(path, sizeof path, "%s/%s", gamedir, e->d_name);
		hqr += packhqr(path, &shrank);
	}
	closedir(d);
	if (hqr == 0)
		fatal("no HQR file in %s", gamedir);
	printf("pack: %d inputs, %d of the HQR files, %d packed and expanded back\n", count+hqr, hqr, shrank);
}

/* One colour searched by both between lo and hi of pal. */
static void
search(uint32_t r, uint32_t v, uint32_t b, uint32_t lo, uint32_t hi)
{
	uint8_t t, o;

	t = lba2_SearchBoundColRGB(r, v, b, pal, lo, hi);
	o = SearchBoundColRGB(r, v, b, pal, lo, hi);
	if (t != o)
		differ("colour: %u %u %u from %u to %u: LBA2's %d, ours %d", r, v, b, lo, hi, t, o);
}

static void
testcolour(void)
{
	uint32_t k, i, n, lo, hi, top;
	long count;

	seed = 1;
	shown = 0;
	count = 0;
	for (k = 0; k < 1000000; k++) {
		n = 1 + rnd(k%2 ? 6 : 256);
		for (i = 0; i < sizeof pal; i++) {
			if (k%3 == 0)
				pal[i] = rnd(64);
			else if (k%3 == 1)
				pal[i] = 4 * rnd(n);
			else
				pal[i] = rnd(256);
		}
		top = k%3 == 0 ? 64 : 256;
		for (i = 0; i < 64; i++) {
			lo = rnd(256);
			hi = rnd(4) == 0 ? lo : rnd(256);
			search(rnd(top), rnd(top), rnd(top), lo, hi);
		}
		search(rnd(256), rnd(256), rnd(256), 0, 255);
		count += 65;
	}
	printf("colour: %ld searches\n", count);
}

static struct {
	char	*name;
	void	(*fn)(void);
} tests[] = {
	{"pack", testpack},
	{"colour", testcolour},
};

int
main(int argc, char **argv)
{
	int i, j, ntests;

	if (argc > 2 && strcmp(argv[1], "-g") == 0) {
		gamedir = argv[2];
		argc -= 2;
		argv += 2;
	}
	if (argc > 1 && argv[1][0] == '-') {
		fprintf(stderr, "usage: lba2oracle [-g gamedir] [test...]\n");
		return 2;
	}
	ntests = sizeof tests / sizeof tests[0];
	if (argc == 1)
		for (j = 0; j < ntests; j++)
			tests[j].fn();
	for (i = 1; i < argc; i++) {
		for (j = 0; j < ntests; j++)
			if (strcmp(argv[i], tests[j].name) == 0)
				break;
		if (j == ntests)
			fatal("no test %s", argv[i]);
		tests[j].fn();
	}
	if (fails > 0) {
		fprintf(stderr, "lba2oracle: %d differ\n", fails);
		return 1;
	}
	printf("lba2oracle: ok\n");
	return 0;
}
