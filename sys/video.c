#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The screen, as lib386/lib_svga's initsvga.asm, s_phys.asm, s_pal.asm
 * and initmode.c drove it: Phys was the VGA window at 0A0000h, 640x480
 * through VESA's banks or 320x200 in mode 13h, and the DAC took 6-bit
 * colours. Here Phys is vram, Screen_X by Screen_Y bytes, presented
 * whole (sys/sdl.c), in the mode last set, and the DAC keeps the same
 * 6-bit values.
 *
 * On Windows a hook of the keyboard, in place while the window has the
 * focus, refuses Alt+F4 (LbaWinSettings's 0x40), and the Windows keys
 * while the screen fills the display (0x80).
 *
 * FEATURE: the wide screen, lba.cfg's WideScreen, wider than 640x480.
 * The game draws in one of two areas, each a place on the screen, its
 * size and its lines' addresses in TabOffLine, which every primitive
 * starts its lines from: the whole screen, where AffScene draws the
 * scene, or the window of 640x480 in its middle, where the interface
 * keeps the 1994 coordinates. Lines go Screen_X apart in both, and the
 * clip is the area's. The port's own.
 */

enum
{
	Width = 640,	/* the window */
	Height = 480,	/* and the screen */
	Nrect = 64,
	Nareas = 8,	/* the areas pushed, at most */
};

/* the areas the game draws in */
enum
{
	Ascreen,	/* the whole screen, in its coordinates */
	Awindow,	/* the window of 640x480 in its middle, the first */
};

typedef struct Rect Rect;

struct Rect
{
	int32_t	x0;
	int32_t	y0;
	int32_t	x1;
	int32_t	y1;
};

uint8_t	*Log;
uint8_t	*Phys;
int32_t	Screen_X = Width;
int32_t	Screen_Y = Height;
int32_t	Wide_X = Width;	/* the screen's width with WideScreen ON, set at the start (sys.c) */
int32_t	FlagWideScreen;	/* WideScreen ON: the screen Wide_X wide */
int32_t	FlagIntegerScaling;	/* IntegerScaling ON: the screen scaled by whole multiples */

/*
 * DesiredDisplayNum is TLBA1C.exe 0x832dc0: the display the window
 * opens on, the nth of SDL's from 0, 0 when the key is missing, as the
 * Classic's image holds it.
 */
int32_t	DesiredDisplayNum;
int32_t	ClipXmin;
int32_t	ClipYmin;
int32_t	ClipXmax = Width - 1;
int32_t	ClipYmax = Height - 1;
uint32_t	TabOffLine[Height];

/*
 * FEATURE: the screen on the whole display or in a window, as lba.cfg's
 * LbaWinFullScreen says, which the option menu's Fullscreen display
 * switches. Taken from the Classic 3.2.3, as LbaWin 0.7.4 made it.
 */

/*
 * FlagFullScreen is TLBA1C.exe 0x818854 (LbaWin.exe 0x43d50c): the
 * screen on the whole display, the Classic's default, as LBA.EXE
 * filled the monitor.
 */
int32_t	FlagFullScreen = 1;

static int32_t memoclip[4] = {0, 0, Width-1, Height-1};
static int current = Awindow;	/* the area drawn in */
static int areas[Nareas];	/* the areas pushed, nareas of them */
static int nareas;
static int32_t areax, areay;	/* the area's place on the screen */
static int32_t areaw = Width;	/* and its size */
static int32_t areah = Height;
static Rect copied[Nrect];
static int ncopied;
static uint32_t copiedshown;

Vga vga;

static void	setarea(int);

/*
 * InitGraphSvga's work: the mode, the line table, Log. The video memory
 * and Log are made for the screen Wide_X wide, the widest the run takes.
 */
void
videoinit(void)
{
	vga.vram = calloc((size_t)Wide_X*Screen_Y, 1);
	if (vga.vram == NULL)
		fatal("no memory for a screen of %dx%d", (int)Wide_X, (int)Screen_Y);
	Phys = vga.vram;
	vga.mode = Svga;
	Log = Malloc(Wide_X*Screen_Y);
	setarea(Awindow);
	sdlscreen();
	/*
	 * FEATURE: the window shown, switched to the whole display first
	 * when FlagFullScreen says so, as the Classic 3.2.3's InitSvga
	 * makes its window.
	 */
	sdlfullscreen(FlagFullScreen);
}

/* SetClip is initsvga.asm's SetClip: no bound is checked against the other. */
void
SetClip(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	ClipXmin = x0 < 0 ? 0 : x0;
	ClipYmin = y0 < 0 ? 0 : y0;
	ClipXmax = x1 < areaw ? x1 : areaw-1;
	ClipYmax = y1 < areah ? y1 : areah-1;
}

void
UnSetClip(void)
{
	ClipXmin = 0;
	ClipYmin = 0;
	ClipXmax = areaw - 1;
	ClipYmax = areah - 1;
}

/* The area at x, y of the screen, w by h: its lines, those past its last following on, and its clip. */
static void
area(int32_t x, int32_t y, int32_t w, int32_t h)
{
	int32_t i;

	areax = x;
	areay = y;
	areaw = w;
	areah = h;
	for (i = 0; i < Screen_Y; i++)
		TabOffLine[i] = (i + y)*Screen_X + x;
	UnSetClip();
}

/*
 * setarea: the area a made the current, its clip the whole area, as
 * the 1994 code sets its clips after each change of area.
 */
static void
setarea(int a)
{
	current = a;
	if (a == Ascreen)
		area(0, 0, Screen_X, Screen_Y);
	else
		area((Screen_X - Width) / 2, 0, Width, Height);
}

static void
pusharea(int a)
{
	if (nareas == Nareas)
		fatal("areas pushed past %d", Nareas);
	areas[nareas++] = current;
	setarea(a);
}

/* pushscreen: the screen's area, till poparea */
void
pushscreen(void)
{
	pusharea(Ascreen);
}

/* pushwindow: the window's area, till poparea */
void
pushwindow(void)
{
	pusharea(Awindow);
}

/* poparea: the area before the last push */
void
poparea(void)
{
	if (nareas == 0)
		fatal("an area popped with none pushed");
	setarea(areas[--nareas]);
}

/*
 * FEATURE: SetSmoothScaling, lba.cfg's SmoothScaling, ON where the key
 * is missing (modes.c's readplatform): the screen scaled through the
 * linear filter where flag is set and sharp otherwise (sdlfilter), the
 * flag given back. The port's own: ON draws as the Classic 3.2.3 draws.
 */
int32_t
SetSmoothScaling(int32_t flag)
{
	sdlfilter(flag != 0);
	return flag;
}

/*
 * A picture of 640x480 loaded at the start of p, a buffer of the
 * screen's size, put in the window's place there, black around it.
 */
void
windowpicture(void *p)
{
	uint8_t *b;
	int32_t x, i;

	b = p;
	x = (Screen_X - Width) / 2;
	for (i = Height - 1; i >= 0; i--) {
		memmove(b + i*Screen_X + x, b + i*Width, Width);
		memset(b + i*Screen_X, 0, x);
		memset(b + i*Screen_X + x + Width, 0, Screen_X - x - Width);
	}
}

void
MemoClip(void)
{
	memoclip[0] = ClipXmin;
	memoclip[1] = ClipYmin;
	memoclip[2] = ClipXmax;
	memoclip[3] = ClipYmax;
}

void
RestoreClip(void)
{
	ClipXmin = memoclip[0];
	ClipYmin = memoclip[1];
	ClipXmax = memoclip[2];
	ClipYmax = memoclip[3];
}

/* Setting a mode clears the video memory, as the BIOS did. */
static void
setmode(int mode)
{
	vga.mode = mode;
	memset(vga.vram, 0, (size_t)Screen_X*Screen_Y);
	displayvsync();
}

void
SimpleInitSvga(void)
{
	setmode(Svga);
}

void
InitMcgaMode(void)
{
	setmode(Mcga);
}

/*
 * FEATURE: SetFullScreen is TLBA1C.exe 0x405000: the window on the whole
 * display or in a window (sdlfullscreen), as FlagFullScreen says, the
 * screen shown again whole, the flag given back as it came. Taken from
 * the Classic 3.2.3.
 */
int32_t
SetFullScreen(int32_t flag)
{
	sdlfullscreen(flag != 0);
	return flag;
}

/*
 * FEATURE: Fullscreen display's choice, the screen switched between the
 * whole display and a window, and the switch written to lba.cfg at
 * once. Taken from the Classic 3.2.3.
 */

/*
 * WriteFullScreen is TLBA1C.exe 0x404500 (LbaWin.exe 0x404d50,
 * ToggleCooperativeLevel): FlagFullScreen turned over, the screen
 * switched, and the flag written to lba.cfg as a number, TimerRef kept
 * across the switch.
 */
void
WriteFullScreen(void)
{
	uint32_t t;

	t = TimerRef;
	/*
	 * FIXED: the Classic turns the flag over by XOR 1, as LbaWin does,
	 * so that a value written by hand other than 0 and 1, 2 say, gives
	 * 3 and 2 again at each choice: the menu says ON and the screen
	 * stays on the whole display. The flag takes its opposite.
	 */
	FlagFullScreen = SetFullScreen(!FlagFullScreen);
	Def_WriteValue((unsigned char*)PathConfigFile, (unsigned char*)"LbaWinFullScreen", FlagFullScreen);
	TimerRef = t;
}

/*
 * FEATURE: Widescreen display's choice, the screen moved at once between
 * 640x480 and the display's shape, and WideScreen written to lba.cfg at
 * once. The port's own.
 */

/*
 * writewide: FlagWideScreen turned over and the screen Wide_X or 640
 * wide, the window's 640x480 of b, a picture of the screen at its old
 * width, laid out again at the new one, black around it, the window's
 * area made again, the screen shown at its new width, and the key
 * written.
 */
void
writewide(void *b)
{
	uint8_t *p;
	int32_t x, i;

	p = b;
	x = (Screen_X - Width) / 2;
	for (i = 0; i < Height; i++)
		memmove(p + i*Width, p + i*Screen_X + x, Width);
	FlagWideScreen = !FlagWideScreen;
	Screen_X = FlagWideScreen ? Wide_X : Width;
	windowpicture(b);
	setarea(current);
	sdlsize();
	Def_WriteString((unsigned char*)PathConfigFile, (unsigned char*)"WideScreen",
		(unsigned char*)(FlagWideScreen ? "ON" : "OFF"));
}

/*
 * The game drew as fast as the machine let it, and the screen showed
 * what it found at each retrace. The main loop waits in framesync. The
 * other loops that draw, the menus', copy the same area again and
 * again: a copy of an area already copied since the screen was last
 * shown waits for the next retrace, so that each one drawn is shown and
 * none is drawn that could not be. An area is kept by its place on the
 * screen.
 */
static void
pace(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	Rect *r;

	if (shown() != copiedshown)
		ncopied = 0;
	for (r = copied; r < copied+ncopied; r++)
		if (r->x0 == x0 && r->y0 == y0 && r->x1 == x1 && r->y1 == y1) {
			vsync();
			ncopied = 0;
			break;
		}
	copiedshown = shown();
	if (ncopied < Nrect)
		copied[ncopied++] = (Rect){x0, y0, x1, y1};
}

/* Flip is s_phys.asm's Flip: Log to the screen, all of it. */
void
Flip(void)
{
	pace(0, 0, Screen_X-1, Screen_Y-1);
	memcpy(vga.vram, Log, (size_t)Screen_X*Screen_Y);
	pump();
}

/* CopyBlockPhys is s_phys.asm's: a rectangle of Log to the same place. */
void
CopyBlockPhys(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	int32_t y, n;

	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 >= areaw)
		x1 = areaw - 1;
	if (y1 >= areah)
		y1 = areah - 1;
	n = x1 - x0 + 1;
	if (n <= 0 || y1 < y0) {
		pump();
		return;
	}
	pace(areax+x0, areay+y0, areax+x1, areay+y1);
	for (y = y0; y <= y1; y++)
		memcpy(vga.vram + TabOffLine[y] + x0, Log + TabOffLine[y] + x0, n);
	pump();
}

/* Vsync waited for the vertical retrace: the next frame shown. */
void
Vsync(void)
{
	vsync();
}

/*
 * FEATURE: PalEntries is TLBA1C.exe 0x837188: the palette Palette last
 * set, whose copy SkipLogo fades after the logo. Taken from the Classic
 * 3.2.3.
 */
uint8_t	PalEntries[768];

/* The DAC took the palette's bytes shifted right by two (s_pal.asm). */
void
Palette(void *pal)
{
	memmove(PalEntries, pal, sizeof PalEntries);
	PalMulti(0, 256, pal);
}

void
PalMulti(int16_t start, int16_t n, uint8_t *pal)
{
	int i;

	for (i = 0; i < n && start+i < 256; i++) {
		vga.dac[start+i][0] = pal[3*i+0] >> 2;
		vga.dac[start+i][1] = pal[3*i+1] >> 2;
		vga.dac[start+i][2] = pal[3*i+2] >> 2;
	}
	pump();
}

void
PalOne(uint8_t c, uint8_t r, uint8_t g, uint8_t b)
{
	vga.dac[c][0] = r >> 2;
	vga.dac[c][1] = g >> 2;
	vga.dac[c][2] = b >> 2;
}
