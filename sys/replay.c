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
 * the byte or b and the word, in hex. A k line holds a second byte, its
 * key's virtual key for the Classic's input (input.c), where that key
 * is other than a US keyboard's, p a pad's input, its number, 0x80
 * added at its release, and v a key that has no XT code, its virtual
 * key, 0x100 added at its release. Its last line holds the pump the
 * run ended at and e. Its first lines name it (# lba input) and give
 * the display the run took (# display WxH) and its frame in ns (# frame
 * N, 0 for unknown), which a replay takes in place of its own, so that
 * the wide screen has the record's width and DisplaySync its pace. A
 * record made before those lines replays as on a display of 640x480 of
 * an unknown rate. The sums are a line per screen shown that differs
 * from the one before: its number, the clock in ns, the sum. When the
 * samples play, they add a line per half of them mixed: w, its number,
 * the sum of its frames. When the CD plays, it adds a line per frame of
 * its audio handed on: c, its number, its sum. When the music's driver
 * is on, it adds a line per period of its timer: m, the chip's samples
 * made so far, the sum of the period's. A PlayStation's track playing
 * in the XMIDI's place adds a line per frame of it handed on: r, its
 * number, its sum. The halves can be written too, raw, 16-bit stereo at
 * the samples' rate, and the chip's samples, at its rate.
 */

static FILE *rec;
static FILE *play;
static FILE *sums;
static FILE *wave;
static FILE *music;
static uint32_t shot = 1, lastshot, shotstep = 1;	/* none unless shotat */
static uint64_t due;
static int duekind;
static unsigned dueval;
static int duevk;	/* a k line's virtual key, or -1 */
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

/*
 * The display w by h the run takes, its window placed, and its frame,
 * which the pace follows, written into the record before any input.
 */
void
recorddisplay(int w, int h)
{
	uint64_t f;

	if (rec == NULL)
		return;
	f = sdlrefresh();
	setrefresh(f);
	fprintf(rec, "# display %dx%d\n# frame %" PRIu64 "\n", w, h, f);
}

/* The next input played, or the end where the record stops short. */
static void
next(void)
{
	char line[128], kind;
	uint64_t p, f;
	unsigned v, vk;
	int n, w, h;

	while (fgets(line, sizeof line, play) != NULL) {
		if (sscanf(line, "# display %dx%d", &w, &h) == 2)
			setdisplay(w, h);
		if (sscanf(line, "# frame %" SCNu64, &f) == 1)
			setrefresh(f);
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;
		v = 0;
		n = sscanf(line, "%" SCNu64 " %c %x %x", &p, &kind, &v, &vk);
		if (n < 2 || (kind != 'e' && n < 3) || strchr("kbpve", kind) == NULL)
			fatal("bad input line: %s", line);
		if (n == 4 && (kind != 'k' || vk > 0xFF))
			fatal("bad input line: %s", line);
		if (kind == 'p' && ((v & 0x7F) >= 31 || v > 0xFF))
			fatal("bad input line: %s", line);
		if (kind == 'v' && ((v & 0xFF) == 0 || v > 0x1FF))
			fatal("bad input line: %s", line);
		due = p;
		duekind = kind;
		dueval = v;
		duevk = n == 4 ? (int)vk : -1;
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
	setdisplay(640, 480);
	setrefresh(0);
}

int
playing(void)
{
	return play != NULL;
}

/* A byte from the keyboard, with its virtual key or -1, to the game but for F4's that altf4 keeps out. */
static void
keyboard(int c, int vk)
{
	if (altf4(c))
		return;
	xtcode(c);
	if (FlagGamePad)
		vkcode(c, vk);
}

/*
 * A byte from the keyboard and its key's virtual key, -1 for a US
 * keyboard's: the system's, unless playing a record, recorded, then
 * given to the game.
 */
void
keyin(int c, int vk)
{
	if (play != NULL)
		return;
	if (rec != NULL && vk >= 0)
		fprintf(rec, "%" PRIu64 " k %x %x\n", pumps(), (unsigned)c, (unsigned)vk);
	else if (rec != NULL)
		fprintf(rec, "%" PRIu64 " k %x\n", pumps(), (unsigned)c);
	keyboard(c, vk);
}

/*
 * The pad's input n down or up: the system's, unless playing a record,
 * recorded, then given to the Classic's input.
 */
void
padin(int n, int down)
{
	if (play != NULL)
		return;
	if (rec != NULL)
		fprintf(rec, "%" PRIu64 " p %x\n", pumps(), (unsigned)(n | (down ? 0 : 0x80)));
	PadButton(n, down);
}

/*
 * The keyboard's key of virtual key vk, which has no XT code, down or
 * up: the system's, unless playing a record, recorded, then given to
 * the Classic's input.
 */
void
vkin(int vk, int down)
{
	if (play != NULL)
		return;
	if (rec != NULL)
		fprintf(rec, "%" PRIu64 " v %x\n", pumps(), vk | (down ? 0 : 0x100));
	vkkey(vk, down);
}

void
wordin(int w)
{
	if (play != NULL)
		return;
	if (rec != NULL)
		fprintf(rec, "%" PRIu64 " b %x\n", pumps(), (unsigned)w);
	if (!altf4word(w))
		biosword(w);
}

/* The inputs played at this pump, and at the record's end, the run's. */
void
replay(uint64_t pump)
{
	while (play != NULL && due <= pump) {
		switch (duekind) {
		case 'k':
			keyboard((int)dueval, duevk);
			break;
		case 'b':
			if (!altf4word((int)dueval))
				biosword((int)dueval);
			break;
		case 'p':
			if (FlagGamePad)
				PadButton((int)(dueval & 0x7F), (dueval & 0x80) == 0);
			break;
		case 'v':
			if (FlagGamePad)
				vkkey((int)(dueval & 0xFF), (dueval & 0x100) == 0);
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

	w = vga.mode == Mcga ? 320 : Screen_X;
	h = vga.mode == Mcga ? 200 : Screen_Y;
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

void
waveto(char *file)
{
	wave = fopen(file, "wb");
	if (wave == NULL)
		fatal("cannot create %s", file);
}

/* The samples' half n, mixed: its sum, and its frames written. */
void
summedwave(uint64_t n, const uint32_t *buf, int frames)
{
	if (sums != NULL)
		fprintf(sums, "w %" PRIu64 " %08" PRIx32 "\n", n, crc(0, (const uint8_t*)buf, (size_t)frames*4));
	if (wave != NULL)
		fwrite(buf, 4, (size_t)frames, wave);
}

/* The CD's frame of audio n, handed on: its sum. */
void
summedcd(uint64_t n, const uint8_t *buf, int len)
{
	if (sums != NULL)
		fprintf(sums, "c %" PRIu64 " %08" PRIx32 "\n", n, crc(0, buf, (size_t)len));
}

/* The PlayStation's track's frame n, handed on at its volume: its sum. */
void
summedpstrack(uint64_t n, uint8_t *buf, int len)
{
	if (sums != NULL)
		fprintf(sums, "r %" PRIu64 " %08" PRIx32 "\n", n, crc(0, buf, (size_t)len));
}

void
musicto(char *file)
{
	music = fopen(file, "wb");
	if (music == NULL)
		fatal("cannot create %s", file);
}

/* The chip's n frames made by the clock's sample s: their sum, and the frames written. */
void
summedmidi(uint64_t s, int16_t *buf, int n)
{
	if (sums != NULL)
		fprintf(sums, "m %" PRIu64 " %08" PRIx32 "\n", s, crc(0, (uint8_t*)buf, (size_t)n*4));
	if (music != NULL)
		fwrite(buf, 4, n, music);
}
