#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * Runs that repeat. The clock moves only as the game goes (timer.c), so
 * a run depends on its input alone. The input is recorded here, each
 * byte from the keyboard (xtcode) and each word for the BIOS's buffer
 * (biosword) with the number of the pump it came in, and played back
 * from such a record instead of the system's, as fast as the machine
 * goes. Each screen shown can be summed, a CRC-32 of its video memory,
 * DAC and mode, and one written as a BMP, to compare two runs, or two
 * builds, frame by frame.
 *
 * A record is text, a line per input: the pump in decimal, then k and
 * the byte or b and the word, in hex. Its last line holds the pump the
 * run ended at and e. The sums are a line per screen shown that differs
 * from the one before: its number, the clock in ns, the sum.
 */

static FILE *rec;
static FILE *play;
static FILE *sums;
static uint32_t shot = 1, lastshot, shotstep = 1;	/* none unless shotat */
static uint64_t due;
static int duekind;
static unsigned dueval;
static uint32_t crctab[256];
static uint32_t lastsum;

/*
 * f written at each line, so that a run ended at once (the watchdog's
 * _Exit) keeps every line. Windows' C library buffers a stream asked
 * for lines whole: there f is written at each call.
 */
static void
bylines(FILE *f)
{
#ifdef _WIN32
	setvbuf(f, NULL, _IONBF, 0);
#else
	setvbuf(f, NULL, _IOLBF, BUFSIZ);
#endif
}

/*
 * The record's end: the pump at whose top the run ended, the one after
 * the last it ran whole, where a replay stops before making anything.
 */
static void
endrecord(void)
{
	fprintf(rec, "%" PRIu64 " e\n", pumps() + 1);
	fclose(rec);
}

void
recordinput(char *file)
{
	rec = fopen(file, "wb");
	if (rec == NULL)
		fatal("cannot create %s", file);
	bylines(rec);
	fprintf(rec, "# lba input\n");
	atexit(endrecord);
}

/* The next input played, or the end where the record stops short. */
static void
next(void)
{
	char line[128], kind;
	uint64_t p;
	unsigned v;
	int n;

	while (fgets(line, sizeof line, play) != NULL) {
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;
		v = 0;
		n = sscanf(line, "%" SCNu64 " %c %x", &p, &kind, &v);
		if (n < 2 || (kind != 'e' && n < 3) || strchr("kbe", kind) == NULL)
			fatal("bad input line: %s", line);
		due = p;
		duekind = kind;
		dueval = v;
		return;
	}
	due = 0;
	duekind = 'e';
}

void
playinput(char *file)
{
	play = fopen(file, "rb");
	if (play == NULL)
		fatal("cannot open %s", file);
	next();
}

int
playing(void)
{
	return play != NULL;
}

/* A byte from the keyboard: the system's, unless playing a record. */
void
keyin(int c)
{
	if (play != NULL)
		return;
	if (rec != NULL)
		fprintf(rec, "%" PRIu64 " k %x\n", pumps(), (unsigned)c);
	xtcode(c);
}

void
wordin(int w)
{
	if (play != NULL)
		return;
	if (rec != NULL)
		fprintf(rec, "%" PRIu64 " b %x\n", pumps(), (unsigned)w);
	biosword(w);
}

/* The inputs played at this pump, and at the record's end, the run's. */
void
replay(uint64_t pump)
{
	while (play != NULL && due <= pump) {
		switch (duekind) {
		case 'k':
			xtcode((int)dueval);
			break;
		case 'b':
			biosword((int)dueval);
			break;
		default:
			portexit(0);
		}
		next();
	}
}

void
sumto(char *file)
{
	sums = fopen(file, "wb");
	if (sums == NULL)
		fatal("cannot create %s", file);
	bylines(sums);
}

/* crc: the CRC-32 of n bytes at p, on from c, a CRC before them or 0. */
static uint32_t
crc(uint32_t c, const uint8_t *p, size_t n)
{
	uint32_t k;
	int i, j;

	if (crctab[1] == 0) {
		for (i = 0; i < 256; i++) {
			k = (uint32_t)i;
			for (j = 0; j < 8; j++)
				k = k & 1 ? 0xEDB88320 ^ k>>1 : k>>1;
			crctab[i] = k;
		}
	}
	c = ~c;
	while (n-- > 0)
		c = crctab[(c ^ *p++) & 0xFF] ^ c>>8;
	return ~c;
}

/* shotat: the screens from n to m to be written, every kth. */
void
shotat(uint32_t n, uint32_t m, uint32_t k)
{
	shot = n;
	lastshot = m;
	shotstep = k != 0 ? k : 1;
}

static void
put16(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
put32(uint8_t *p, uint32_t v)
{
	put16(p, v);
	put16(p+2, v>>16);
}

/* The screen as an 8-bit BMP: headers, palette, rows from the bottom. */
static void
writebmp(char *file, int w, int h)
{
	uint8_t hdr[54], pal[1024], *d;
	FILE *f;
	int i, y;

	memset(hdr, 0, sizeof hdr);
	hdr[0] = 'B';
	hdr[1] = 'M';
	put32(hdr+2, sizeof hdr + sizeof pal + (uint32_t)w*h);
	put32(hdr+10, sizeof hdr + sizeof pal);
	put32(hdr+14, 40);
	put32(hdr+18, (uint32_t)w);
	put32(hdr+22, h);
	put16(hdr+26, 1);
	put16(hdr+28, 8);
	put32(hdr+34, (uint32_t)w*h);
	put32(hdr+46, 256);
	for (i = 0; i < 256; i++) {
		d = vga.dac[i];
		pal[4*i+0] = (uint8_t)(d[2]<<2 | d[2]>>4);
		pal[4*i+1] = (uint8_t)(d[1]<<2 | d[1]>>4);
		pal[4*i+2] = (uint8_t)(d[0]<<2 | d[0]>>4);
		pal[4*i+3] = 0;
	}
	f = fopen(file, "wb");
	if (f == NULL)
		fatal("cannot create %s", file);
	fwrite(hdr, 1, sizeof hdr, f);
	fwrite(pal, 1, sizeof pal, f);
	for (y = h - 1; y >= 0; y--)
		fwrite(vga.vram + y*w, 1, (size_t)w, f);
	if (fclose(f) != 0)
		fatal("cannot write %s", file);
}

/* The screen shown n at clock t: its sum if it changed, and the shot. */
void
summed(uint32_t n, uint64_t t)
{
	char file[32];
	uint32_t s;
	uint8_t m;
	int w, h;

	w = vga.mode == Mcga ? 320 : 640;
	h = vga.mode == Mcga ? 200 : 480;
	if (sums != NULL) {
		m = vga.mode;
		s = crc(0, vga.vram, (size_t)w*h);
		s = crc(s, &vga.dac[0][0], sizeof vga.dac);
		s = crc(s, &m, 1);
		if (s != lastsum) {
			fprintf(sums, "%" PRIu32 " %" PRIu64 " %08" PRIx32 "\n", n, t, s);
			lastsum = s;
		}
	}
	if (n >= shot && n <= lastshot && (n-shot) % shotstep == 0) {
		snprintf(file, sizeof file, "lba%" PRIu32 ".bmp", n);
		writebmp(file, w, h);
	}
}
