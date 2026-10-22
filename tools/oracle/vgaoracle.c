#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "dat.h"
#include "fns.h"
#include "abi.h"
#include "pc.h"

/*
 * vgaoracle: the screen as lib386/lib_svga's initsvga.asm, vesa.asm,
 * s_phys.asm and s_pal.asm drive VESA's BIOS and the VGA, with
 * sources/mcga.asm in mode 13h, against sys/video.c and sources/mcga.c,
 * the port's, at the interface the game calls: the screen's pixels, the
 * DAC's colours, the clip, and the retrace Vsync waits for.
 *
 *	vgaoracle lba0.elf lba0.sym
 *
 * lba0.elf is a DOS build, as tools/le2elf.py makes it, and lba0.sym
 * names its procedures and data (tools/wmap2sym.py of its link map).
 * Its code runs on pc.c's PC and a VGA of the model's. 2 MB of video
 * memory, of which VESA's window A shows 64 KB at A0000h from where its
 * function 4F05h puts it, in units of the card's granularity. The
 * BIOS's int 10h, 0Fh and 00h for the mode, 4F01h for mode 101h's
 * information, which DPMI's 0300h passes down, and 4F02h, each mode set
 * clearing the memory and loading the BIOS's palette. The DAC at 3C8h
 * and 3C9h, which keeps 6 bits. The retrace at 3DAh, whose bit 3 is set
 * for the 2 lines from each frame's start on the model's clock: VGA's
 * 25.175 MHz, 800 dots a line, 525 lines at 640x480 and 449 in mode
 * 13h. A read of 3DAh moves the clock a microsecond on, and a poll that
 * finds its bit as it left it goes to the bit's next change.
 * sys/video.c draws in its video memory, its clock (sys/timer.c) moved
 * by idle.
 *
 *	init	InitSvga and videoinit: the lines' table, the clip, the
 *		memory cleared, mode 101h set through VESA, OldVideo kept
 *	copies	30000 copies at random, each CopyBlockPhys or Flip, of Log
 *		changed at random between, on five cards: granularities of
 *		64, 16 and 4 KB, lines of 640 bytes and of 1024, which
 *		makes NonStdVESA's copies: rectangles anywhere, across the
 *		banks' edges, a pixel, the game's constant ones, the screen
 *	palette	20000 calls at random of Palette, PalMulti over the
 *		game's ranges (the holomap's 32 from 192, the movies'
 *		Palette alone) and any in the DAC, and PalOne
 *	clip	SetClip, UnSetClip, MemoClip and RestoreClip at random
 *	waits	5000 Vsyncs from places at random, a quarter of them in
 *		the retrace, in both modes, the frames passed counted on
 *		both sides
 *	modes	the movies' and the zoom's switches, InitMcgaMode then
 *		Palette and Mcga_Flip, SimpleInitSvga then Palette and
 *		Flip, 300 times: the memory, the DAC, DOS memory given
 *		back
 *
 * The screen's pixels must be the same, 640x480 or 320x200 as the mode
 * says, the lines of the card's pitch read for the port's of 640, after
 * init, each copy and each step of the switches, the DAC after each
 * palette call and each switch's Palette, the clip after each clip
 * call, the frames each wait passes, and every call acall makes must
 * keep the convention. Then the assembly alone:
 *
 *	convention	SimpleInitSvga, which the zoom's and the movies'
 *			ends call as C does, keeps the registers C keeps
 *	driver	a card's driver given its variables' address in EAX by
 *		SimpleInitSvga as InitSvga gives it, as a driver's InitMode
 *		writes through it
 *	no VESA	4F01h failing: VESA_Error set, the DOS block given back,
 *		the mode left as it was
 *	caller's CX	InitSvga and SimpleInitSvga entered with ECX
 *		5555h, which the game's code may leave: DPMI's 0300h
 *		given CX 0
 *
 * What the port does otherwise on purpose is let pass: its DAC kept
 * across a mode set, where the BIOS loads its own, which the game
 * replaces at once, and a screen shown at its clock's pumps, which the
 * palette's calls but PalOne's make.
 *
 * Each part runs in a process of its own, ended after 600 seconds, or
 * 60 for the assembly alone, its differences reported, the first 20 in
 * full, and the end is "vgaoracle: ok", or "vgaoracle: N failed" and
 * status 1.
 */

enum
{
	Width = 640,
	Height = 480,
	Vram = 2<<20,
	Window = 0xa0000,
	Winsize = 0x10000,
	Mcgasize = 320*200,
	Step = 1000,	/* ns, a read of 3DAh */
	Dot = 25175000,	/* Hz */
	Retrace = 2,	/* lines */
	Ncard = 5,
	Ncopy = 30000,
	Npal = 20000,
	Nsync = 5000,
	Nmode = 300,
};

typedef struct Card Card;

/* a VESA card of the model's */
struct Card
{
	char	*name;
	int	gran;	/* KB */
	int	pitch;	/* bytes a line in mode 101h */
};

static Card cards[Ncard] = {
	{"64 KB, 640", 64, 640},
	{"16 KB, 640", 16, 640},
	{"4 KB, 640", 4, 640},
	{"64 KB, 1024", 64, 1024},
	{"4 KB, 1024", 4, 1024},
};

/* the assembly's procedures and data, by lba0.sym */
static struct
{
	uintptr_t	initsvga;
	uintptr_t	simpleinitsvga;
	uintptr_t	initmcgamode;
	uintptr_t	setclip;
	uintptr_t	unsetclip;
	uintptr_t	memoclip;
	uintptr_t	restoreclip;
	uintptr_t	vsync;
	uintptr_t	copyblockphys;
	uintptr_t	flip;
	uintptr_t	palette;
	uintptr_t	palmulti;
	uintptr_t	palone;
	uintptr_t	mcgacls;
	uintptr_t	mcgaflip;
	int32_t	*clip;	/* ClipXmin, ClipYmin, ClipXmax, ClipYmax */
	uint32_t	*taboffline;
	uint8_t	*oldvideo;
	uint8_t	*log;	/* where Log is, unaligned */
	uint32_t	*initsvgamode;
	uint8_t	*vesaerror;
} as;

/* sys/video.c's, sys/timer.c's and sources/mcga.c's */
extern uint8_t	*Log;
extern int32_t	ClipXmin, ClipYmin, ClipXmax, ClipYmax;
extern uint32_t	TabOffLine[];
void	SetClip(int32_t, int32_t, int32_t, int32_t);
void	UnSetClip(void);
void	MemoClip(void);
void	RestoreClip(void);
void	SimpleInitSvga(void);
void	InitMcgaMode(void);
void	Flip(void);
void	CopyBlockPhys(int32_t, int32_t, int32_t, int32_t);
void	Vsync(void);
void	Palette(void*);
void	PalOne(uint8_t, uint8_t, uint8_t, uint8_t);
void	Mcga_Cls(void);
void	Mcga_Flip(void);

/* the model's VGA */
static int vramfd;
static uint8_t *vram;	/* all of it */
static Card *card;
static int mode = 3;	/* the BIOS's */
static uint32_t winpos;	/* window A's place, in granularity units */
static uint8_t dac[256][3];
static int dacindex, dacphase;
static uint64_t mnow;	/* the model's clock, ns */
static int lastbit = -1;	/* 3DAh's bit 3 at the poll before, -1 before the first */
static int nmodeset;

static uint64_t lastshown;	/* the port's last retrace shown, its clock's */
static uint8_t *alog;	/* the assembly's Log */
uint32_t drveax;	/* EAX a driver's InitMode was given, which drvmode's assembly stores */
static int failvesa;	/* 4F01h fails */

/* sys/sdl.c's, sys/mem.c's, sys/sys.c's, sys/replay.c's and lib_sys's, for sys/video.c and sys/timer.c */
char	PathConfigFile[1];

void*
Malloc(int32_t n)
{
	void *p;

	p = calloc(n > 0 ? n : 1, 1);
	if (p == NULL)
		fatal("out of memory");
	return p;
}

void
sdlscreen(void)
{
}

void
sdlfullscreen(int on)
{
	USED(on);
}

void
sdlfilter(int on)
{
	USED(on);
}

void
sdlsize(void)
{
}

int32_t
Def_WriteValue(unsigned char *path, unsigned char *key, int32_t v)
{
	USED(path);
	USED(key);
	return v;
}

int32_t
Def_WriteString(unsigned char *path, unsigned char *key, unsigned char *s)
{
	USED(path);
	USED(key);
	USED(s);
	return 1;
}

void
sdlpoll(void)
{
}

void
replay(uint64_t n)
{
	USED(n);
}

void
wavetick(uint64_t t)
{
	USED(t);
}

void
cdtick(uint64_t t)
{
	USED(t);
}

void
miditick(uint64_t t)
{
	USED(t);
}

void
sdlshow(void)
{
}

uint64_t
sdlrefresh(void)
{
	return 0;
}

uint64_t
sdlwindowframe(void)
{
	return 0;
}

void
sdlvsync(int n)
{
	USED(n);
}

/* The port's screen shown, at the retrace t. */
void
summed(uint32_t n, uint64_t t)
{
	USED(n);
	lastshown = t;
}

uint64_t
sdlnanos(void)
{
	return 0;
}

void
sdlsleep(uint64_t t)
{
	USED(t);
}

void
sdllate(void)
{
}

/* A replay's: the clock runs as fast as the oracle calls. */
int
playing(void)
{
	return 1;
}

/* The port's end, sdl.c's: the C library's exit. */
void
portexit(int status)
{
	exit(status);
}

/*
 * drvmode is a card's driver's InitMode, as InitSvga calls it through
 * InitSvgaMode, which keeps the EAX it is given, the address of the
 * variables a driver sets.
 */
void	drvmode(void);

__asm__(
	".text\n"
	".globl drvmode\n"
	"drvmode:\n"
	"	movl %eax, drveax\n"
	"	ret\n"
);

static uint32_t
rnd(void)
{
	static uint32_t x = 0x1b873593;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

/* A frame of the mode's, in ns, as sys/timer.c's frame counts it. */
static uint64_t
frame(void)
{
	return (uint64_t)800 * (mode == 0x13 ? 449 : 525) * 1000000000 / Dot;
}

static int
inretrace(uint64_t t)
{
	return t % frame() < (uint64_t)800 * Retrace * 1000000000 / Dot;
}

/* The clock at bit 3's next change after t. */
static uint64_t
nextedge(uint64_t t)
{
	uint64_t f, r, base;

	f = frame();
	r = (uint64_t)800 * Retrace * 1000000000 / Dot;
	base = t - t%f;
	if (t%f < r)
		return base + r;
	return base + f;
}

/* Window A at its place, showing the memory from there. */
static void
window(void)
{
	void *m;
	uint64_t off;

	off = mode == 0x13 ? 0 : (uint64_t)winpos * card->gran * 1024;
	if (off + Winsize > Vram) {
		fail("window A at %u, past the memory", winpos);
		runend();
	}
	m = mmap((void*)Window, Winsize, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, vramfd, off);
	if (m != (void*)Window)
		fatal("cannot map window A");
}

/* A mode set: the memory cleared, the window at 0, the BIOS's palette. */
static void
setmode(int m)
{
	int i;

	mode = m;
	winpos = 0;
	memset(vram, 0, Vram);
	for (i = 0; i < 256; i++) {
		dac[i][0] = i & 63;
		dac[i][1] = i >> 2;
		dac[i][2] = (255 - i) >> 2;
	}
	dacindex = 0;
	dacphase = 0;
	window();
	nmodeset++;
}

/* The VESA BIOS's int 10h. */
static void
int10(Cpu *c)
{
	uint8_t *b;

	switch (c->eax & 0xffff) {
	case 0x4f01:
		if (!c->real) {
			fail("4F01h without DPMI's 0300h, its buffer a protected-mode address");
			runend();
		}
		if ((c->ecx & 0xffff) != 0x101) {
			fail("4F01h for mode %xh", c->ecx & 0xffff);
			runend();
		}
		if (failvesa) {
			setlo(&c->eax, 0x014f);
			return;
		}
		b = cpumem(c, c->es, c->edi, 256);
		memset(b, 0, 256);
		put16(b, 0x9b);
		b[2] = 7;
		put16(b+4, card->gran);
		put16(b+6, 64);
		put16(b+8, 0xa000);
		put16(b+0x10, card->pitch);
		put16(b+0x12, Width);
		put16(b+0x14, Height);
		b[0x16] = 8;
		b[0x17] = 16;
		b[0x18] = 1;
		b[0x19] = 8;
		b[0x1a] = 1;
		b[0x1b] = 4;
		setlo(&c->eax, 0x004f);
		return;
	case 0x4f02:
		if ((c->ebx & 0x3fff) != 0x101) {
			fail("4F02h for mode %xh", c->ebx & 0xffff);
			runend();
		}
		setmode(0x101);
		setlo(&c->eax, 0x004f);
		return;
	case 0x4f05:
		if ((c->ebx & 0xffff) != 0) {
			fail("4F05h, BX %04xh", c->ebx & 0xffff);
			runend();
		}
		winpos = c->edx & 0xffff;
		window();
		setlo(&c->eax, 0x004f);
		return;
	}
	switch (c->eax >> 8 & 0xff) {
	case 0x0f:
		setlo(&c->eax, 80 << 8 | (mode == 0x101 ? 0x6a : mode));
		c->ebx &= ~0xff00u;
		return;
	case 0x00:
		if ((c->eax & 0x7f) != 0x13 && (c->eax & 0x7f) != 0x03) {
			fail("int 10h, mode %02xh", c->eax & 0xff);
			runend();
		}
		setmode(c->eax & 0x7f);
		return;
	}
	fail("int 10h, function %04x, which the model lacks", c->eax & 0xffff);
	runend();
}

/* The VGA's ports: the DAC's write index and data, the retrace. */
static uint32_t
vgain(uint32_t port, int w)
{
	int b;

	USED(w);
	if (port != 0x3da) {
		fail("the VGA's port %xh read", port);
		return 0;
	}
	b = inretrace(mnow);
	if (b == lastbit) {
		mnow = nextedge(mnow);
		b = inretrace(mnow);
	}
	lastbit = b;
	mnow += Step;
	return b ? 0x09 : 0x00;
}

static void
vgaout(uint32_t port, uint32_t v, int w)
{
	if (w != 1) {
		fail("the VGA's port %xh written %d bytes at once", port, w);
		return;
	}
	switch (port) {
	case 0x3c8:
		dacindex = v & 0xff;
		dacphase = 0;
		return;
	case 0x3c9:
		dac[dacindex][dacphase] = v & 63;
		if (++dacphase == 3) {
			dacphase = 0;
			dacindex = (dacindex + 1) & 255;
		}
		return;
	}
	fail("the VGA's port %xh written, %02xh", port, v);
}

/* The screen the same on both sides: 1 if it is. */
static int
samescreen(void)
{
	uint8_t *a, *p;
	int y, x, w, h, pitch;

	if ((mode == 0x13) != (vga.mode == Mcga)) {
		fail("the assembly's mode %xh, ours %s", mode, vga.mode == Mcga ? "13h" : "101h");
		return 0;
	}
	w = mode == 0x13 ? 320 : Width;
	h = mode == 0x13 ? 200 : Height;
	pitch = mode == 0x13 ? 320 : card->pitch;
	for (y = 0; y < h; y++) {
		a = vram + y*pitch;
		p = vga.vram + y*w;
		if (memcmp(a, p, w) == 0)
			continue;
		for (x = 0; a[x] == p[x]; x++)
			;
		fail("the screen at %d,%d: the assembly's %02x, ours %02x", x, y, a[x], p[x]);
		return 0;
	}
	return 1;
}

static int
samedac(void)
{
	int i;

	for (i = 0; i < 256; i++)
		if (memcmp(dac[i], vga.dac[i], 3) != 0) {
			fail("colour %d: the assembly's %02x %02x %02x, ours %02x %02x %02x", i,
				dac[i][0], dac[i][1], dac[i][2],
				vga.dac[i][0], vga.dac[i][1], vga.dac[i][2]);
			return 0;
		}
	return 1;
}

static int
sameclip(void)
{
	if (as.clip[0] == ClipXmin && as.clip[1] == ClipYmin && as.clip[2] == ClipXmax && as.clip[3] == ClipYmax)
		return 1;
	fail("the assembly's clip %d,%d to %d,%d, ours %d,%d to %d,%d", as.clip[0], as.clip[1],
		as.clip[2], as.clip[3], ClipXmin, ClipYmin, ClipXmax, ClipYmax);
	return 0;
}

/* Both Logs changed alike: n runs of bytes at random, or all of it. */
static void
scribble(int n)
{
	uint32_t o, k, i;

	if (n == 0) {
		for (i = 0; i < Width*Height; i++)
			alog[i] = Log[i] = rnd();
		return;
	}
	while (n-- > 0) {
		o = rnd() % (Width*Height);
		k = 1 + rnd() % 2000;
		if (o + k > Width*Height)
			k = Width*Height - o;
		for (i = o; i < o+k; i++)
			alog[i] = Log[i] = rnd();
	}
}

/* Mode 101h on both sides, as the game's start sets it, on card c. */
static void
initboth(Card *c)
{
	card = c;
	clockinit();
	videoinit();
	alog = Malloc(Width*Height);
	put32(as.log, (uint32_t)(uintptr_t)alog);
	snprintf(what, sizeof what, "InitSvga");
	acall(as.initsvga, 0);
	if (nmodeset != 1 || mode != 0x101)
		fail("%d mode sets, the last %xh", nmodeset, mode);
}

static void
init(void *v)
{
	int i;

	USED(v);
	initboth(&cards[0]);
	if (*as.oldvideo != 3)
		fail("OldVideo %02xh, the BIOS's mode 03h", *as.oldvideo);
	for (i = 0; i < Height; i++)
		if (as.taboffline[i] != TabOffLine[i]) {
			fail("TabOffLine[%d]: the assembly's %u, ours %u", i, as.taboffline[i], TabOffLine[i]);
			break;
		}
	sameclip();
	samescreen();
	printf("init: %s\n", nfail == 0 ? "the same" : "differs");
}

/* A rectangle as the game copies them. */
static void
anyrect(int32_t *r)
{
	static int32_t game[][4] = {
		{5, 446, 100, 479}, {16, 400, 623, 440}, {0, 0, 639, 240}, {170, 50, 470, 330},
		{0, 300, 199, 479}, {100, 100, 550, 290}, {0, 0, 639, 99}, {0, 0, 639, 479},
	};
	static int edge[] = {102, 204, 307, 409, 64, 128, 192, 256, 320, 384, 448};
	int32_t t;
	int k;

	k = rnd() % 10;
	if (k < 5) {
		r[0] = rnd() % Width;
		r[1] = rnd() % Height;
		r[2] = rnd() % Width;
		r[3] = rnd() % Height;
	} else if (k < 7) {
		r[1] = edge[rnd() % nelem(edge)] - rnd() % 3;
		r[3] = r[1] + rnd() % 4;
		r[0] = rnd() % Width;
		r[2] = rnd() % Width;
	} else if (k < 8) {
		r[0] = r[2] = rnd() % Width;
		r[1] = r[3] = rnd() % Height;
	} else {
		memmove(r, game[rnd() % nelem(game)], 4 * sizeof r[0]);
	}
	if (r[0] > r[2]) {
		t = r[0];
		r[0] = r[2];
		r[2] = t;
	}
	if (r[1] > r[3]) {
		t = r[1];
		r[1] = r[3];
		r[3] = t;
	}
}

static void
copies(void *v)
{
	int32_t r[4];
	int i;

	initboth(v);
	scribble(0);
	for (i = 0; i < Ncopy; i++) {
		scribble(1 + rnd() % 8);
		if (rnd() % 10 == 0) {
			snprintf(what, sizeof what, "copy %d, Flip", i);
			acall(as.flip, 0);
			Flip();
		} else {
			anyrect(r);
			snprintf(what, sizeof what, "copy %d, CopyBlockPhys(%d, %d, %d, %d)", i,
				r[0], r[1], r[2], r[3]);
			acall(as.copyblockphys, 4, r[0], r[1], r[2], r[3]);
			CopyBlockPhys(r[0], r[1], r[2], r[3]);
		}
		if (!samescreen())
			break;
	}
	printf("copies, %s: %d copies, %s\n", card->name, i, nfail == 0 ? "the same" : "differ");
}

static void
palette(void *v)
{
	uint8_t pal[768];
	int i, j, s, n, c;

	USED(v);
	initboth(&cards[0]);
	for (j = 0; j < 768; j++)
		pal[j] = rnd();
	snprintf(what, sizeof what, "the first Palette");
	acall(as.palette, 1, (int32_t)(uintptr_t)pal);
	Palette(pal);
	for (i = 0; i < Npal; i++) {
		for (j = 0; j < 768; j++)
			pal[j] = rnd();
		switch (rnd() % 4) {
		case 0:
			snprintf(what, sizeof what, "call %d, Palette", i);
			acall(as.palette, 1, (int32_t)(uintptr_t)pal);
			Palette(pal);
			break;
		case 1:
			snprintf(what, sizeof what, "call %d, PalMulti(192, 32)", i);
			acall(as.palmulti, 3, 192, 32, (int32_t)(uintptr_t)pal);
			PalMulti(192, 32, pal);
			break;
		case 2:
			s = rnd() % 256;
			n = 1 + rnd() % (256 - s);
			snprintf(what, sizeof what, "call %d, PalMulti(%d, %d)", i, s, n);
			acall(as.palmulti, 3, s, n, (int32_t)(uintptr_t)pal);
			PalMulti(s, n, pal);
			break;
		default:
			c = rnd() % 256;
			snprintf(what, sizeof what, "call %d, PalOne(%d, %d, %d, %d)", i, c, pal[0], pal[1], pal[2]);
			acall(as.palone, 4, c, pal[0], pal[1], pal[2]);
			PalOne(c, pal[0], pal[1], pal[2]);
			break;
		}
		if (dacphase != 0)
			fail("the DAC left %d values into a colour", dacphase);
		if (!samedac())
			break;
	}
	printf("palette: %d calls, %s\n", i, nfail == 0 ? "the same" : "differ");
}

static void
clip(void *v)
{
	int32_t x0, y0, x1, y1;
	int i;

	USED(v);
	initboth(&cards[0]);
	for (i = 0; i < 100000; i++) {
		switch (rnd() % 4) {
		case 0:
			x0 = (int32_t)(rnd() % 1400) - 300;
			y0 = (int32_t)(rnd() % 1100) - 300;
			x1 = (int32_t)(rnd() % 1400) - 300;
			y1 = (int32_t)(rnd() % 1100) - 300;
			snprintf(what, sizeof what, "SetClip(%d, %d, %d, %d)", x0, y0, x1, y1);
			acall(as.setclip, 4, x0, y0, x1, y1);
			SetClip(x0, y0, x1, y1);
			break;
		case 1:
			snprintf(what, sizeof what, "UnSetClip");
			acall(as.unsetclip, 0);
			UnSetClip();
			break;
		case 2:
			snprintf(what, sizeof what, "MemoClip");
			acall(as.memoclip, 0);
			MemoClip();
			break;
		default:
			snprintf(what, sizeof what, "RestoreClip");
			acall(as.restoreclip, 0);
			RestoreClip();
			break;
		}
		if (!sameclip())
			break;
	}
	printf("clip: %d calls, %s\n", i, nfail == 0 ? "the same" : "differ");
}

/*
 * Waits from places at random, a quarter of them in the retrace, the
 * port's clock moved by idles and pumps, the model's alike, the frames
 * passed counted on both sides. Mode 13h's waits with mcga set.
 */
static void
waits(void *v)
{
	uint64_t f, w;
	int i, k, m, p, mcga;

	mcga = v != NULL;
	initboth(&cards[0]);
	if (mcga) {
		acall(as.initmcgamode, 0);
		InitMcgaMode();
	}
	for (i = 0; i < Nsync; i++) {
		f = frame();
		m = rnd() % 40;
		w = (uint64_t)m * 1010000;
		p = 0;
		if (rnd() % 4 == 0)
			p = (f - (mnow + w) % f + 30000) / 10000;
		else if ((mnow + w) % f < 30000 || (mnow + w) % f > f - 30000)
			p = 5;
		w += (uint64_t)p * 10000;
		for (k = 0; k < m; k++)
			idle();
		for (k = 0; k < p; k++)
			pump();
		mnow += w;
		snprintf(what, sizeof what, "wait %d in mode %s, %d ms and %d pumps", i,
			mcga ? "13h" : "101h", m, p);
		lastbit = -1;
		acall(as.vsync, 0);
		Vsync();
		if (mnow / f != lastshown / f) {
			fail("the assembly at frame %u, ours %u", (uint32_t)(mnow / f), (uint32_t)(lastshown / f));
			break;
		}
		if (!inretrace(mnow - Step))
			fail("the assembly returned out of the retrace");
	}
	printf("waits, mode %s: %d waits, %u frames, %s\n", mcga ? "13h" : "101h", i,
		(uint32_t)(mnow / frame()), nfail == 0 ? "the same" : "differ");
}

/* DOS memory all given back. */
static void
dosgiven(void)
{
	uint32_t a;

	a = lowalloc(65536 - 16);
	if (a != Lowbase)
		fail("DOS memory kept at %x", Lowbase);
}

static void
modes(void *v)
{
	uint8_t pal[768];
	int i, j;

	USED(v);
	initboth(&cards[0]);
	for (i = 0; i < Nmode; i++) {
		for (j = 0; j < 768; j++)
			pal[j] = rnd();
		snprintf(what, sizeof what, "switch %d, InitMcgaMode and Palette", i);
		acall(as.initmcgamode, 0);
		InitMcgaMode();
		samescreen();
		acall(as.palette, 1, (int32_t)(uintptr_t)pal);
		Palette(pal);
		samedac();
		snprintf(what, sizeof what, "switch %d, Mcga_Cls and Mcga_Flip", i);
		scribble(4);
		acall(as.mcgaflip, 0);
		Mcga_Flip();
		samescreen();
		acall(as.mcgacls, 0);
		Mcga_Cls();
		for (j = 0; j < Mcgasize; j++)
			if (alog[j] != 0 || Log[j] != 0) {
				fail("Log at %d after Mcga_Cls: the assembly's %02x, ours %02x", j, alog[j], Log[j]);
				break;
			}
		for (j = 0; j < 768; j++)
			pal[j] = rnd();
		snprintf(what, sizeof what, "switch %d, SimpleInitSvga and Palette", i);
		cxcall(as.simpleinitsvga, 0, NULL, 0);
		SimpleInitSvga();
		samescreen();
		acall(as.palette, 1, (int32_t)(uintptr_t)pal);
		Palette(pal);
		samedac();
		snprintf(what, sizeof what, "switch %d, Flip", i);
		scribble(0);
		acall(as.flip, 0);
		Flip();
		if (!samescreen() || nfail > 0)
			break;
	}
	snprintf(what, sizeof what, "the end");
	dosgiven();
	printf("modes: %d switches, %d mode sets, %s\n", i, nmodeset, nfail == 0 ? "the same" : "differ");
}

/* The assembly alone. */
static void
faultrun(void *v)
{
	char *name;
	uint32_t m;
	Regs r;

	name = v;
	card = &cards[0];
	alog = Malloc(Width*Height);
	put32(as.log, (uint32_t)(uintptr_t)alog);
	if (strcmp(name, "convention") == 0) {
		snprintf(what, sizeof what, "InitSvga");
		acall(as.initsvga, 0);
		snprintf(what, sizeof what, "SimpleInitSvga");
		acall(as.simpleinitsvga, 0);
	} else if (strcmp(name, "driver") == 0) {
		m = (uint32_t)(uintptr_t)drvmode;
		memmove(as.initsvgamode, &m, 4);
		snprintf(what, sizeof what, "InitSvga");
		drveax = 0;
		acall(as.initsvga, 0);
		if (drveax != lookup("ScanLine"))
			fail("the driver given EAX %08x, ScanLine is %08x", drveax, (uint32_t)lookup("ScanLine"));
		snprintf(what, sizeof what, "SimpleInitSvga");
		drveax = 0;
		memset(&r, 0, sizeof r);
		r.eax = 0x77777777;
		regcall(as.simpleinitsvga, &r);
		if (drveax != lookup("ScanLine"))
			fail("the driver given EAX %08x, ScanLine is %08x", drveax, (uint32_t)lookup("ScanLine"));
	} else if (strcmp(name, "no VESA") == 0) {
		failvesa = 1;
		snprintf(what, sizeof what, "InitSvga");
		acall(as.initsvga, 0);
		if (*as.vesaerror != 1)
			fail("VESA_Error %d", *as.vesaerror);
		if (nmodeset != 0)
			fail("%d mode sets", nmodeset);
		dosgiven();
	} else if (strcmp(name, "caller's CX") == 0) {
		snprintf(what, sizeof what, "InitSvga");
		ncx = 0;
		cxcall(as.initsvga, 0, NULL, 0x5555);
		cxnone();
		snprintf(what, sizeof what, "SimpleInitSvga");
		cxcall(as.simpleinitsvga, 0, NULL, 0x5555);
		cxnone();
	}
	printf("%s: %s\n", name, nfail == 0 ? "as it should" : "fails");
}

int
main(int argc, char **argv)
{
	static struct
	{
		char	*name;
		void	(*fn)(void*);
	} parts[] = {
		{"init", init},
		{"palette", palette},
		{"clip", clip},
		{"waits", waits},
		{"waits, mode 13h", waits},
		{"modes", modes},
	};
	static char *faults[] = {"convention", "driver", "no VESA", "caller's CX"};
	char label[Nlabel];
	int i, nbad;
	uint32_t *t;

	oracle = "vgaoracle";
	if (argc != 3) {
		fprintf(stderr, "usage: vgaoracle lba0.elf lba0.sym\n");
		return 2;
	}
	pcload(argv[1], argv[2]);
	as.initsvga = lookup("InitSvga");
	as.simpleinitsvga = lookup("SimpleInitSvga");
	as.initmcgamode = lookup("InitMcgaMode");
	as.setclip = lookup("SetClip");
	as.unsetclip = lookup("UnSetClip");
	as.memoclip = lookup("MemoClip");
	as.restoreclip = lookup("RestoreClip");
	as.vsync = lookup("Vsync");
	as.copyblockphys = lookup("CopyBlockPhys");
	as.flip = lookup("Flip");
	as.palette = lookup("Palette");
	as.palmulti = lookup("PalMulti");
	as.palone = lookup("PalOne");
	as.mcgacls = lookup("Mcga_Cls");
	as.mcgaflip = lookup("Mcga_Flip");
	as.clip = (int32_t*)lookup("ClipXmin");
	as.taboffline = (uint32_t*)lookup("TabOffLine");
	as.oldvideo = (uint8_t*)lookup("OldVideo");
	as.log = (uint8_t*)lookup("Log");
	as.initsvgamode = (uint32_t*)lookup("InitSvgaMode");
	as.vesaerror = (uint8_t*)lookup("VESA_Error");
	t = (uint32_t*)lookup("ClipYmax");
	if ((int32_t*)t != as.clip + 3)
		fatal("the clip's variables apart");
	vramfd = memfd_create("vram", 0);
	if (vramfd < 0 || ftruncate(vramfd, Vram) < 0)
		fatal("cannot make the video memory");
	vram = mmap(NULL, Vram, PROT_READ|PROT_WRITE, MAP_SHARED, vramfd, 0);
	if (vram == MAP_FAILED)
		fatal("cannot map the video memory");
	card = &cards[0];
	window();
	pcint(0x10, int10);
	pcport(0x3c0, 0x3df, vgain, vgaout);
	nbad = 0;
	for (i = 0; i < (int)nelem(parts); i++)
		nbad += inchild(parts[i].name, 600, parts[i].fn,
			strcmp(parts[i].name, "waits, mode 13h") == 0 ? parts : NULL);
	for (i = 0; i < Ncard; i++) {
		snprintf(label, sizeof label, "copies, %s", cards[i].name);
		nbad += inchild(label, 600, copies, &cards[i]);
	}
	for (i = 0; i < (int)nelem(faults); i++)
		nbad += inchild(faults[i], 60, faultrun, faults[i]);
	if (nbad > 0) {
		fprintf(stderr, "vgaoracle: %d failed\n", nbad);
		return 1;
	}
	printf("vgaoracle: ok\n");
	return 0;
}
