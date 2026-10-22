#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dat.h"
#include "fns.h"

/*
 * The screen, as lib386/lib_svga's initsvga.asm, s_phys.asm, s_pal.asm
 * and initmode.c drove it: Phys was the VGA window at 0A0000h, 640x480
 * through VESA's banks or 320x200 in mode 13h, and the DAC took 6-bit
 * colours. Here Phys is vram, presented whole (sys/sdl.c), in the mode
 * last set, and the DAC keeps the same 6-bit values.
 *
 * On Windows a hook of the keyboard, in place while the window has the
 * focus, refuses Alt+F4 (LbaWinSettings's 0x40), and the Windows keys
 * while the screen fills the display (0x80).
 */

enum
{
	Width = 640,
	Height = 480,
	Nrect = 64,
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
static Rect copied[Nrect];
static int ncopied;
static uint32_t copiedshown;

Vga vga;

/* InitGraphSvga's work: the mode, the line table, Log. */
void
videoinit(void)
{
	int y;

	Phys = vga.vram;
	vga.mode = Svga;
	for (y = 0; y < Height; y++)
		TabOffLine[y] = y * Screen_X;
	Log = Malloc(Width*Height);
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
	ClipXmax = x1 < Screen_X ? x1 : Screen_X-1;
	ClipYmax = y1 < Screen_Y ? y1 : Screen_Y-1;
}

void
UnSetClip(void)
{
	ClipXmin = 0;
	ClipYmin = 0;
	ClipXmax = Screen_X - 1;
	ClipYmax = Screen_Y - 1;
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
	memset(vga.vram, 0, sizeof vga.vram);
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
 * The game drew as fast as the machine let it, and the screen showed
 * what it found at each retrace. The main loop waits in framesync. The
 * other loops that draw, the menus', copy the same area again and
 * again: a copy of an area already copied since the screen was last
 * shown waits for the next retrace, so that each one drawn is shown and
 * none is drawn that could not be.
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
	pace(0, 0, Width-1, Height-1);
	memcpy(vga.vram, Log, Width*Height);
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
	if (x1 >= Width)
		x1 = Width - 1;
	if (y1 >= Height)
		y1 = Height - 1;
	n = x1 - x0 + 1;
	if (n <= 0 || y1 < y0) {
		pump();
		return;
	}
	pace(x0, y0, x1, y1);
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

/* The DAC took the palette's bytes shifted right by two (s_pal.asm). */
void
Palette(void *pal)
{
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
