#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * textoracle: run s_string.asm's 8x8 text against ours.
 *
 *	textoracle [test...]
 *
 * s_string.asm is assembled by JWasm in the flat model, every symbol
 * prefixed asm_ (the Makefile), and draws on a screen of its own. Ours
 * is lib386/lib_svga/s_string.c on the port's. Both screens hold the
 * same random bytes, with 8192 more before and past each, and after each
 * call they and the colours CoulText keeps must be the same.
 *
 * The tests, each named on the command line, all by default:
 *
 *	glyphs	each character alone at every eighth place of a line, in
 *		four pairs of colours, the paper 0xFF among them
 *	strings	20000 strings of random characters, up to 80, at random
 *		places, the last line and a line's end among them, in
 *		random colours, of 32 bits as the game's -1 is, the paper
 *		0xFF one in two
 *	wide	our text on 400 screens 640 to 1704 wide, the window of 640
 *		by 480 in their middle as the wide screen lays it out,
 *		against the assembly's on the window's lines copied to a
 *		screen of 640 and back
 */

enum
{
	Width = 640,
	Height = 480,
	Widest = 1704,
	Slack = 8192,	/* before and past each screen, compared too */
	Span = Slack + Widest*Height + Slack,
	Rounds = 20000,
	Shown = 10,	/* differences printed */
};

void	asm_AffString(int32_t, int32_t, void*);
void	asm_CoulText(int32_t, int32_t);
void	AffString(int32_t, int32_t, void*);
void	CoulText(int32_t, int32_t);

/* the assembly's state, which svga.ash names */
uint8_t	*asm_Log;
uint32_t	asm_TabOffLine[Height];
int32_t	asm_Screen_X = Width;
uint8_t	asm_Text_Ink, asm_Text_Paper;

/* ours, which the port's video.c defines, and s_string.c's colours */
uint8_t	*Log;
uint32_t	TabOffLine[Height];
int32_t	Screen_X = Width;
extern uint8_t	Text_Ink, Text_Paper;

static uint8_t *theirs, *ours, *model;	/* the screens and their slack */
static uint32_t span;	/* the bytes of a screen and its slack */
static uint32_t seed;
static int fails, shown;

static void
fatal(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	fprintf(stderr, "textoracle: ");
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
	fprintf(stderr, "textoracle: ");
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

static uint8_t*
screenbuf(void)
{
	uint8_t *p;

	p = malloc(Span);
	if (p == NULL)
		fatal("out of memory");
	return p;
}

/* Both sides on screens of w bytes a line, the window of 640 at x, the same random bytes. */
static void
screens(int32_t w, int32_t x)
{
	uint32_t i;
	int y;

	span = Slack + w*Height + Slack;
	for (i = 0; i < span; i++)
		theirs[i] = rnd(256);
	memcpy(ours, theirs, span);
	asm_Log = theirs + Slack;
	Log = ours + Slack;
	asm_Screen_X = Width;
	Screen_X = w;
	for (y = 0; y < Height; y++) {
		asm_TabOffLine[y] = y*Width;
		TabOffLine[y] = y*w + x;
	}
}

/* The first byte where a and b differ, from the screen's start. */
static int32_t
at(uint8_t *a, uint8_t *b)
{
	int32_t k;

	for (k = 0; a[k] == b[k]; k++)
		;
	return k - Slack;
}

/* Both colours set, and compared. */
static void
colours(int32_t ink, int32_t paper)
{
	asm_CoulText(ink, paper);
	CoulText(ink, paper);
	if (asm_Text_Ink != Text_Ink || asm_Text_Paper != Text_Paper)
		differ("CoulText %#x %#x: ours %#x %#x, the assembly's %#x %#x", (unsigned)ink, (unsigned)paper,
			Text_Ink, Text_Paper, asm_Text_Ink, asm_Text_Paper);
}

/* s drawn at x,y by both, and their screens compared. */
static void
draw(int32_t x, int32_t y, uint8_t *s)
{
	int32_t k;

	asm_AffString(x, y, s);
	AffString(x, y, s);
	if (memcmp(theirs, ours, span) != 0) {
		k = at(theirs, ours);
		differ("AffString of %d characters at %d %d in %#x on %#x: byte %d (%d,%d)", (int)strlen((char*)s),
			x, y, Text_Ink, Text_Paper, k, k%Width, k/Width);
		memcpy(ours, theirs, span);
	}
}

static void
testglyphs(void)
{
	static int32_t pairs[][2] = {{15, -1}, {15, 0}, {0, 15}, {0x44, 0xAE}};
	uint8_t s[2];
	int c, i;

	screens(Width, 0);
	s[1] = 0;
	for (i = 0; i < (int)(sizeof pairs / sizeof pairs[0]); i++) {
		colours(pairs[i][0], pairs[i][1]);
		for (c = 1; c < 256; c++) {
			s[0] = c;
			draw(8*(c%80), 8*(c/80) + 40*i, s);
		}
	}
	printf("glyphs: %d characters in %d colours\n", 255, i);
}

/* A random string of up to max characters into s. */
static void
randstring(uint8_t *s, int max)
{
	int i, n;

	n = rnd(max + 1);
	for (i = 0; i < n; i++)
		s[i] = 1 + rnd(255);
	s[n] = 0;
}

static void
teststrings(void)
{
	uint8_t s[81];
	int32_t x, y, paper;
	int r;

	screens(Width, 0);
	for (r = 0; r < Rounds; r++) {
		if (r % 1000 == 0)
			screens(Width, 0);
		paper = rnd(2) == 0 ? -1 : (int32_t)(rnd(1<<16) << 16 | rnd(1<<16));
		colours(rnd(1<<16) << 16 | rnd(1<<16), paper);
		randstring(s, 80);
		switch (rnd(4)) {
		case 0:
			x = Width - 8;
			y = Height - 8;
			break;
		case 1:
			x = -8 + rnd(Width + 8);
			y = rnd(Height);
			break;
		default:
			x = rnd(Width - 8*(int)strlen((char*)s) + 1);
			y = rnd(Height - 7);
			break;
		}
		draw(x, y, s);
	}
	printf("strings: %d rounds\n", r);
}

/* The window of 640 at x of a screen w wide, line by line, from wide to a screen of 640 or back. */
static void
window(uint8_t *narrow, uint8_t *wide, int32_t w, int32_t x, int back)
{
	uint8_t *n, *d;
	int y;

	for (y = 0; y < Height; y++) {
		n = narrow + Slack + y*Width;
		d = wide + Slack + y*w + x;
		if (back)
			memcpy(d, n, Width);
		else
			memcpy(n, d, Width);
	}
}

static void
testwide(void)
{
	uint8_t s[81];
	int32_t w, x, tx, ty, k;
	int r;

	for (r = 0; r < 400; r++) {
		w = Width + rnd(Widest - Width + 1);
		x = (w - Width) / 2;
		screens(w, x);
		window(theirs, ours, w, x, 0);
		memcpy(model, ours, span);
		colours(rnd(256), rnd(2) == 0 ? -1 : (int32_t)rnd(256));
		randstring(s, 80);
		tx = rnd(Width - 8*(int)strlen((char*)s) + 1);
		ty = rnd(Height - 7);
		asm_AffString(tx, ty, s);
		AffString(tx, ty, s);
		window(theirs, model, w, x, 1);
		if (memcmp(model, ours, span) != 0) {
			k = at(model, ours);
			differ("AffString of %d characters at %d %d on %d: byte %d (%d,%d)", (int)strlen((char*)s),
				tx, ty, w, k, k%w, k/w);
		}
	}
	printf("wide: %d screens\n", r);
}

static struct {
	char	*name;
	void	(*fn)(void);
} tests[] = {
	{"glyphs", testglyphs},
	{"strings", teststrings},
	{"wide", testwide},
};

int
main(int argc, char **argv)
{
	int i, j, ntests;

	if (argc > 1 && argv[1][0] == '-') {
		fprintf(stderr, "usage: textoracle [test...]\n");
		return 2;
	}
	theirs = screenbuf();
	ours = screenbuf();
	model = screenbuf();
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
		fprintf(stderr, "textoracle: %d differ\n", fails);
		return 1;
	}
	printf("textoracle: ok\n");
	return 0;
}
